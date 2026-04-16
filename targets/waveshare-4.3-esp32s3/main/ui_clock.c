/**
 * @file  ui_clock.c
 * @brief Analog station-clock style widget for the 4.3" panel test.
 *
 * This is a scaled adaptation of the 7" prototype clock. The widget draws
 * a static face in LV_EVENT_DRAW_MAIN and the hands in LV_EVENT_DRAW_POST.
 * Only the hand areas are invalidated when time changes, which keeps the
 * 4.3" panel calm and avoids full-object redraw flicker.
 */

#include "ui_clock.h"
#include "ui_theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ── Geometry for the 500 × 480 right-side page ───────────────────── */
#define CLOCK_D      380
#define CLOCK_R      (CLOCK_D / 2)
#define CLOCK_X      ((500 - CLOCK_D) / 2)
#define CLOCK_Y      28
#define BEZEL_W       12
#define FACE_R       (CLOCK_R - BEZEL_W)

#define TICK_OUT_R   (FACE_R - 4)
#define TICK_MAJ_LEN  14
#define TICK_MIN_LEN   7

#define NUM_R        140
#define NUM_BOX_W     38
#define NUM_BOX_H     26
#define BRAND_Y_OFFSET 72
#define DATE_LABEL_W  220
#define DATE_LABEL_X  (500 - 22 - DATE_LABEL_W)
#define DATE_LABEL_Y  437

#define HAND_H_TIP    92
#define HAND_H_TAIL  -16
#define HAND_M_TIP   124
#define HAND_M_TAIL  -20
#define HAND_S_TIP   158
#define HAND_S_TAIL  -34
#define SECOND_STEP_RAD (2.0f * (float)M_PI / 60.0f)
#define SECOND_BOUNCE_FRACTION 0.12f

/* ── Palette ──────────────────────────────────────────────────────── */
static lv_color_t clr_bg(void)      { return ui_theme_is_night_mode() ? lv_color_hex(0x000000) : lv_color_hex(0x0F141F); }
static lv_color_t clr_face(void)    { return ui_theme_is_night_mode() ? lv_color_hex(0x0A0A0A) : lv_color_hex(0xF1F0EB); }
static lv_color_t clr_ring(void)    { return ui_theme_is_night_mode() ? lv_color_hex(0x141414) : lv_color_hex(0x1B1E24); }
static lv_color_t clr_hand(void)    { return ui_theme_is_night_mode() ? lv_color_hex(0xA78958) : lv_color_hex(0xB79258); }
static lv_color_t clr_tick_min(void){ return ui_theme_is_night_mode() ? lv_color_hex(0x44413C) : lv_color_hex(0x9A9A96); }
static lv_color_t clr_tick_maj(void){ return ui_theme_is_night_mode() ? lv_color_hex(0x7C7266) : lv_color_hex(0x46433E); }
static lv_color_t clr_numeral(void) { return ui_theme_is_night_mode() ? lv_color_hex(0x93887B) : lv_color_hex(0x2F3238); }
static lv_color_t clr_brand(void)   { return ui_theme_is_night_mode() ? lv_color_hex(0x575046) : lv_color_hex(0x7A7F88); }
static lv_color_t clr_date(void)    { return ui_theme_is_night_mode() ? lv_color_hex(0x6B6358) : lv_color_hex(0x808799); }

static lv_point_t s_tick_out[60];
static lv_point_t s_tick_in[60];
static lv_area_t  s_num_area[12];
static const char *s_num_str[12] = {
    "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"
};

static float s_angle_h = 0.0f;
static float s_angle_m = 0.0f;
static float s_angle_s_drawn = 0.0f;
static float s_angle_s_target = 0.0f;

static lv_obj_t *s_clock_bg_obj = NULL;
static lv_obj_t *s_clock_obj = NULL;
static lv_obj_t *s_lbl_date = NULL;

static void get_clock_center(int32_t *cx, int32_t *cy)
{
    lv_area_t oa;
    lv_obj_get_coords(s_clock_obj, &oa);
    *cx = oa.x1 + CLOCK_R;
    *cy = oa.y1 + CLOCK_R;
}

static void get_hand_bounds(int32_t cx, int32_t cy, float angle,
                            int32_t tail_r, int32_t tip_r, int32_t width,
                            lv_area_t *area)
{
    float sa = sinf(angle), ca = cosf(angle);
    int32_t x1 = cx + (int32_t)(tail_r * sa);
    int32_t y1 = cy - (int32_t)(tail_r * ca);
    int32_t x2 = cx + (int32_t)(tip_r  * sa);
    int32_t y2 = cy - (int32_t)(tip_r  * ca);
    int32_t pad = width + 4;

    area->x1 = LV_MIN(x1, x2) - pad;
    area->y1 = LV_MIN(y1, y2) - pad;
    area->x2 = LV_MAX(x1, x2) + pad;
    area->y2 = LV_MAX(y1, y2) + pad;
}

static void join_area(lv_area_t *dst, const lv_area_t *src)
{
    dst->x1 = LV_MIN(dst->x1, src->x1);
    dst->y1 = LV_MIN(dst->y1, src->y1);
    dst->x2 = LV_MAX(dst->x2, src->x2);
    dst->y2 = LV_MAX(dst->y2, src->y2);
}

static void invalidate_hand_pair(float old_angle, float new_angle,
                                 int32_t tail_r, int32_t tip_r, int32_t width)
{
    if (s_clock_obj == NULL) return;

    int32_t cx, cy;
    get_clock_center(&cx, &cy);

    lv_area_t dirty;
    get_hand_bounds(cx, cy, old_angle, tail_r, tip_r, width, &dirty);

    lv_area_t next;
    get_hand_bounds(cx, cy, new_angle, tail_r, tip_r, width, &next);
    join_area(&dirty, &next);

    lv_area_t hub = { cx - 10, cy - 10, cx + 10, cy + 10 };
    join_area(&dirty, &hub);

    lv_obj_invalidate_area(s_clock_obj, &dirty);
}

static void draw_hand(lv_layer_t *layer, lv_draw_line_dsc_t *dsc,
                      int32_t cx, int32_t cy, float angle,
                      int32_t tail_r, int32_t tip_r)
{
    float sa = sinf(angle), ca = cosf(angle);
    dsc->p1.x = cx + (int32_t)(tail_r * sa);
    dsc->p1.y = cy - (int32_t)(tail_r * ca);
    dsc->p2.x = cx + (int32_t)(tip_r  * sa);
    dsc->p2.y = cy - (int32_t)(tip_r  * ca);
    lv_draw_line(layer, dsc);
}

static void draw_bg_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_target(e);

    lv_area_t oa;
    lv_obj_get_coords(obj, &oa);
    int32_t cx = oa.x1 + CLOCK_R;
    int32_t cy = oa.y1 + CLOCK_R;

    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = clr_ring();
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.radius = LV_RADIUS_CIRCLE;
    rdsc.border_width = 0;
    rdsc.shadow_width = 0;
    lv_draw_rect(layer, &rdsc, &oa);

    lv_area_t fa = {
        oa.x1 + BEZEL_W, oa.y1 + BEZEL_W,
        oa.x2 - BEZEL_W, oa.y2 - BEZEL_W
    };
    rdsc.bg_color = clr_face();
    lv_draw_rect(layer, &rdsc, &fa);

    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.round_start = 1;
    ldsc.round_end = 1;
    for (int i = 0; i < 60; i++) {
        bool major = (i % 5 == 0);
        ldsc.color = major ? clr_tick_maj() : clr_tick_min();
        ldsc.width = major ? 3 : 2;
        ldsc.p1.x = cx + s_tick_in[i].x;
        ldsc.p1.y = cy + s_tick_in[i].y;
        ldsc.p2.x = cx + s_tick_out[i].x;
        ldsc.p2.y = cy + s_tick_out[i].y;
        lv_draw_line(layer, &ldsc);
    }

    lv_draw_label_dsc_t tdsc;
    lv_draw_label_dsc_init(&tdsc);
    tdsc.font = &lv_font_montserrat_20;
    tdsc.color = clr_numeral();
    tdsc.opa = LV_OPA_COVER;
    tdsc.align = LV_TEXT_ALIGN_CENTER;
    for (int i = 0; i < 12; i++) {
        lv_area_t la = {
            oa.x1 + s_num_area[i].x1,
            oa.y1 + s_num_area[i].y1,
            oa.x1 + s_num_area[i].x2,
            oa.y1 + s_num_area[i].y2,
        };
        tdsc.text = s_num_str[i];
        lv_draw_label(layer, &tdsc, &la);
    }

    tdsc.font = &lv_font_montserrat_16;
    tdsc.color = clr_brand();
    {
        lv_area_t la = { cx - 50, cy - 62, cx + 50, cy - 36 };
        tdsc.text = "BUC";
        lv_draw_label(layer, &tdsc, &la);
    }
    {
        lv_area_t la = { cx - 78, cy + BRAND_Y_OFFSET - 10, cx + 78, cy + BRAND_Y_OFFSET + 14 };
        tdsc.text = "ChaoticVolt";
        lv_draw_label(layer, &tdsc, &la);
    }
}

static void draw_hands_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_target(e);

    lv_area_t oa;
    lv_obj_get_coords(obj, &oa);
    int32_t cx = oa.x1 + CLOCK_R;
    int32_t cy = oa.y1 + CLOCK_R;

    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.round_start = 1;
    ldsc.round_end = 1;
    ldsc.color = clr_hand();

    ldsc.width = 7;
    draw_hand(layer, &ldsc, cx, cy, s_angle_h, HAND_H_TAIL, HAND_H_TIP);

    ldsc.width = 5;
    draw_hand(layer, &ldsc, cx, cy, s_angle_m, HAND_M_TAIL, HAND_M_TIP);

    ldsc.width = 2;
    draw_hand(layer, &ldsc, cx, cy, s_angle_s_drawn, HAND_S_TAIL, HAND_S_TIP);

    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = clr_hand();
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.radius = LV_RADIUS_CIRCLE;
    rdsc.border_width = 0;
    rdsc.shadow_width = 0;
    lv_area_t hub = { cx - 7, cy - 7, cx + 7, cy + 7 };
    lv_draw_rect(layer, &rdsc, &hub);
}

static void anim_sec_cb(void *var, int32_t val)
{
    LV_UNUSED(var);
    float next = (float)val * 0.001f;
    invalidate_hand_pair(s_angle_s_drawn, next, HAND_S_TAIL, HAND_S_TIP, 2);
    s_angle_s_drawn = next;
}

static void anim_sec_settle_cb(lv_anim_t *a)
{
    lv_anim_t settle;
    lv_anim_init(&settle);
    lv_anim_set_var(&settle, a->var);
    lv_anim_set_exec_cb(&settle, anim_sec_cb);
    lv_anim_set_duration(&settle, 70);
    lv_anim_set_values(&settle,
        (int32_t)(s_angle_s_drawn * 1000.0f),
        (int32_t)(s_angle_s_target * 1000.0f));
    lv_anim_set_path_cb(&settle, lv_anim_path_ease_out);
    lv_anim_start(&settle);
}

void ui_clock_create(lv_obj_t *parent)
{
    for (int i = 0; i < 60; i++) {
        float a = (float)i * (2.0f * (float)M_PI / 60.0f);
        float sa = sinf(a), ca = cosf(a);
        int32_t r_out = TICK_OUT_R;
        int32_t r_in = r_out - (i % 5 == 0 ? TICK_MAJ_LEN : TICK_MIN_LEN);
        s_tick_out[i].x = (lv_coord_t)(r_out * sa);
        s_tick_out[i].y = (lv_coord_t)(-r_out * ca);
        s_tick_in[i].x = (lv_coord_t)(r_in * sa);
        s_tick_in[i].y = (lv_coord_t)(-r_in * ca);
    }

    for (int i = 0; i < 12; i++) {
        float a = ((float)(i * 30) - 90.0f) * ((float)M_PI / 180.0f);
        float nx = CLOCK_R + (float)NUM_R * cosf(a);
        float ny = CLOCK_R + (float)NUM_R * sinf(a);
        s_num_area[i].x1 = (lv_coord_t)(nx - NUM_BOX_W / 2);
        s_num_area[i].y1 = (lv_coord_t)(ny - NUM_BOX_H / 2);
        s_num_area[i].x2 = s_num_area[i].x1 + NUM_BOX_W;
        s_num_area[i].y2 = s_num_area[i].y1 + NUM_BOX_H;
    }

    lv_obj_t *bg = lv_obj_create(parent);
    s_clock_bg_obj = bg;
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, CLOCK_D, CLOCK_D);
    lv_obj_set_pos(bg, CLOCK_X, CLOCK_Y);
    lv_obj_set_style_bg_color(bg, clr_bg(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(bg, draw_bg_cb, LV_EVENT_DRAW_MAIN, NULL);

    lv_obj_t *fg = lv_obj_create(parent);
    s_clock_obj = fg;
    lv_obj_remove_style_all(fg);
    lv_obj_set_size(fg, CLOCK_D, CLOCK_D);
    lv_obj_set_pos(fg, CLOCK_X, CLOCK_Y);
    lv_obj_set_style_bg_opa(fg, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(fg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(fg, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(fg, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(fg, draw_hands_cb, LV_EVENT_DRAW_POST, NULL);

    s_lbl_date = lv_label_create(parent);
    lv_label_set_text(s_lbl_date, "---");
    lv_obj_set_style_text_font(s_lbl_date, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_date, clr_date(), 0);
    lv_obj_set_width(s_lbl_date, DATE_LABEL_W);
    lv_obj_set_style_text_align(s_lbl_date, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_lbl_date, DATE_LABEL_X, DATE_LABEL_Y);
    lv_obj_add_flag(s_lbl_date, LV_OBJ_FLAG_HIDDEN);
}

void ui_clock_tick(void)
{
    if (s_clock_obj == NULL) return;

    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);

    int h = t.tm_hour % 12;
    int m = t.tm_min;
    int s = t.tm_sec;

    const float TWO_PI = 2.0f * (float)M_PI;
    float old_angle_h = s_angle_h;
    float old_angle_m = s_angle_m;
    float old_angle_s = s_angle_s_drawn;

    s_angle_h = TWO_PI * ((float)(h * 60 + m) / 720.0f);
    s_angle_m = TWO_PI * ((float)m / 60.0f);

    float s_new = TWO_PI * ((float)s / 60.0f);
    static int s_prev_second = -1;

    lv_anim_del(s_clock_obj, anim_sec_cb);

    if (s_prev_second >= 0 && s == ((s_prev_second + 1) % 60) && s_prev_second != 59) {
        float overshoot = s_new + (SECOND_STEP_RAD * SECOND_BOUNCE_FRACTION);
        s_angle_s_target = s_new;

        lv_anim_t bounce;
        lv_anim_init(&bounce);
        lv_anim_set_var(&bounce, s_clock_obj);
        lv_anim_set_exec_cb(&bounce, anim_sec_cb);
        lv_anim_set_completed_cb(&bounce, anim_sec_settle_cb);
        lv_anim_set_duration(&bounce, 95);
        lv_anim_set_values(&bounce,
            (int32_t)(old_angle_s * 1000.0f),
            (int32_t)(overshoot * 1000.0f));
        lv_anim_set_path_cb(&bounce, lv_anim_path_ease_out);
        lv_anim_start(&bounce);
    } else {
        s_angle_s_target = s_new;
        s_angle_s_drawn = s_new;
        invalidate_hand_pair(old_angle_s, s_angle_s_drawn, HAND_S_TAIL, HAND_S_TIP, 2);
    }

    if (fabsf(s_angle_h - old_angle_h) > 0.0001f) {
        invalidate_hand_pair(old_angle_h, s_angle_h, HAND_H_TAIL, HAND_H_TIP, 7);
    }
    if (fabsf(s_angle_m - old_angle_m) > 0.0001f) {
        invalidate_hand_pair(old_angle_m, s_angle_m, HAND_M_TAIL, HAND_M_TIP, 5);
    }

    char buf[48];
    strftime(buf, sizeof(buf), "%A, %d %B %Y", &t);
    lv_label_set_text(s_lbl_date, buf);

    s_prev_second = s;
}

void ui_clock_apply_theme(void)
{
    if (s_clock_bg_obj == NULL || s_clock_obj == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(s_clock_bg_obj, clr_bg(), 0);
    if (s_lbl_date) {
        lv_obj_set_style_text_color(s_lbl_date, clr_date(), 0);
    }
    lv_obj_invalidate(s_clock_bg_obj);
    lv_obj_invalidate(s_clock_obj);
}
