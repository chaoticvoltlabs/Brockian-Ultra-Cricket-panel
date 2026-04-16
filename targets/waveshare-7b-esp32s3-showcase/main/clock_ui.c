/**
 * clock_ui.c  –  ECharts-style analog wall clock, bare-metal custom draw.
 *
 * NO lv_meter.  One lv_obj with two draw-event callbacks:
 *   LV_EVENT_DRAW_MAIN  – static background (bezel, face, ticks, numerals, text)
 *   LV_EVENT_DRAW_POST  – clock hands (hour, minute, second) on top of everything
 *
 * All tick-mark endpoints and numeral positions are precomputed once at
 * clock_ui_create() and stored in static arrays.  The draw callbacks do
 * nothing but memcpy-class lv_draw_* calls; no LVGL widget machinery runs
 * on the hot path.
 *
 * Hand animation:
 *   The second hand does an overshoot-bounce via lv_anim.  The exec-callback
 *   stores the interpolated angle and invalidates the clock object so LVGL
 *   schedules a redraw.  Because full_refresh=1 is set in display.c, the
 *   entire 1024×600 frame is always re-rendered (keeps both PSRAM framebuffers
 *   coherent for the double-buffer swap).
 */

#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "clock_ui.h"
#include "panel_alert.h"

static const char *TAG = "clock_ui";

/* ── Geometry ──────────────────────────────────────────────────────────────── */
#define CLOCK_R      236   /* outer radius (face diameter = 472 px)          */
#define BEZEL_W       16   /* bezel ring pixel width                         */
#define FACE_R       (CLOCK_R - BEZEL_W)

#define TICK_OUT_R   (FACE_R - 4)          /* tick outer edge from centre    */
#define TICK_MAJ_LEN  16                   /* major tick length (px)         */
#define TICK_MIN_LEN   8                   /* minor tick length (px)         */

#define NUM_R        176   /* numeral centre radius from clock centre         */
#define NUM_BOX_W     48   /* label box width  (centred on NUM_R point)       */
#define NUM_BOX_H     32   /* label box height                                */

/* Hand geometry (positive = tip, negative = counterbalance behind pivot) */
#define HAND_H_TIP   112
#define HAND_H_TAIL  -20
#define HAND_M_TIP   150
#define HAND_M_TAIL  -25
#define HAND_S_TIP   190
#define HAND_S_TAIL  -45

/* ── Palette ───────────────────────────────────────────────────────────────── */
#define CLR_BG        lv_color_hex(0x090909)
#define CLR_FACE      lv_color_hex(0x121212)
#define CLR_RING      lv_color_hex(0x1B1B1B)
#define CLR_HAND      lv_color_hex(0xC1962E)
#define CLR_TICK_MIN  lv_color_hex(0x3D3A35)
#define CLR_TICK_MAJ  lv_color_hex(0x7B7059)
#define CLR_NUMERAL   lv_color_hex(0xB7A479)
#define CLR_BRAND     lv_color_hex(0x6E6658)
#define CLR_DATE      lv_color_hex(0xC8BA94)
#define CLR_ACCENT    lv_color_hex(0x49443A)
#define CLR_SUBTLE    lv_color_hex(0x8B836F)

#define CLOCK_CENTER_X 664
#define CLOCK_CENTER_Y 286

#define BRAND_TOP_Y_OFFSET      72
#define BRAND_BOTTOM_Y_OFFSET   92

#define PANEL_DATE_X            116
#define PANEL_DATE_Y             92
#define PANEL_DATE_W            240

#define PANEL_ROOM_X            116
#define PANEL_ROOM_Y            412
#define PANEL_ROOM_W            240

#define PANEL_ROOM_TEMP_Y       PANEL_ROOM_Y
#define PANEL_ROOM_RH_Y         (PANEL_ROOM_Y + 58)

/* ── Precomputed geometry (filled by clock_ui_create) ─────────────────────── */
static lv_point_t s_tick_out[60];   /* tick outer endpoint, relative to cx,cy */
static lv_point_t s_tick_in [60];   /* tick inner endpoint, relative to cx,cy */
static lv_area_t  s_num_area[12];   /* numeral draw area, relative to obj x1,y1 */
static const char *s_num_str[12] = {
    "12","1","2","3","4","5","6","7","8","9","10","11"
};

/* ── Hand angles (radians, 0 = 12-o'clock, clockwise) ─────────────────────── */
static float s_angle_h    = 0.0f;
static float s_angle_m    = 0.0f;
static float s_angle_s    = 0.0f;   /* current target (set by tick)            */
static float s_angle_s_drawn = 0.0f; /* actual drawn angle (driven by anim)   */

/* ── Widget handles ────────────────────────────────────────────────────────── */
static lv_obj_t *s_clock_bg_obj = NULL;
static lv_obj_t *s_clock_obj    = NULL;
static lv_obj_t *s_lbl_date     = NULL;
static lv_obj_t *s_lbl_room_temp = NULL;
static lv_obj_t *s_lbl_room_rh   = NULL;
static lv_obj_t *s_parent        = NULL;

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
    if (s_clock_obj == NULL) {
        return;
    }

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

/* ─────────────────────────────────────────────────────────────────────────────
 * draw_hand  –  draw a single hand from (tail_r) to (tip_r) through the pivot
 * ───────────────────────────────────────────────────────────────────────────── */
static void draw_hand(lv_draw_ctx_t *ctx, lv_draw_line_dsc_t *dsc,
                      int32_t cx, int32_t cy, float angle,
                      int32_t tail_r, int32_t tip_r)
{
    float sa = sinf(angle), ca = cosf(angle);
    lv_point_t p_tail = { cx + (int32_t)(tail_r * sa), cy - (int32_t)(tail_r * ca) };
    lv_point_t p_tip  = { cx + (int32_t)(tip_r  * sa), cy - (int32_t)(tip_r  * ca) };
    lv_draw_line(ctx, dsc, &p_tail, &p_tip);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * LV_EVENT_DRAW_MAIN  –  static background
 * ───────────────────────────────────────────────────────────────────────────── */
static void draw_bg_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t      *obj      = lv_event_get_target(e);

    lv_area_t oa;
    lv_obj_get_coords(obj, &oa);
    int32_t cx = oa.x1 + CLOCK_R;
    int32_t cy = oa.y1 + CLOCK_R;

    /* ── Bezel (filled dark circle) ────────────────────────────────────── */
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color     = CLR_RING;
    rdsc.bg_opa       = LV_OPA_COVER;
    rdsc.radius       = LV_RADIUS_CIRCLE;
    rdsc.border_width = 0;
    rdsc.shadow_width = 0;
    lv_draw_rect(draw_ctx, &rdsc, &oa);

    /* ── Cream face (inner circle) ──────────────────────────────────────── */
    lv_area_t fa = {
        oa.x1 + BEZEL_W, oa.y1 + BEZEL_W,
        oa.x2 - BEZEL_W, oa.y2 - BEZEL_W
    };
    rdsc.bg_color = CLR_FACE;
    lv_draw_rect(draw_ctx, &rdsc, &fa);

    /* ── Tick marks (60 total) ──────────────────────────────────────────── */
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.round_start = 1;
    ldsc.round_end   = 1;
    for (int i = 0; i < 60; i++) {
        bool major = (i % 5 == 0);
        ldsc.color = major ? CLR_TICK_MAJ : CLR_TICK_MIN;
        ldsc.width = major ? 3 : 2;
        lv_point_t p1 = { cx + s_tick_in [i].x, cy + s_tick_in [i].y };
        lv_point_t p2 = { cx + s_tick_out[i].x, cy + s_tick_out[i].y };
        lv_draw_line(draw_ctx, &ldsc, &p1, &p2);
    }

    /* ── 12 Arabic numerals ─────────────────────────────────────────────── */
    lv_draw_label_dsc_t tdsc;
    lv_draw_label_dsc_init(&tdsc);
    tdsc.font  = &lv_font_montserrat_28;
    tdsc.color = CLR_NUMERAL;
    tdsc.opa   = LV_OPA_COVER;
    tdsc.align = LV_TEXT_ALIGN_CENTER;
    for (int i = 0; i < 12; i++) {
        lv_area_t la = {
            oa.x1 + s_num_area[i].x1,
            oa.y1 + s_num_area[i].y1,
            oa.x1 + s_num_area[i].x2,
            oa.y1 + s_num_area[i].y2,
        };
        lv_draw_label(draw_ctx, &tdsc, &la, s_num_str[i], NULL);
    }

    /* ── Upper brand ─────────────────────────────────────────────────────── */
    tdsc.font  = &lv_font_montserrat_20;
    tdsc.color = CLR_BRAND;
    tdsc.align = LV_TEXT_ALIGN_CENTER;
    {
        lv_area_t la = {
            cx - 48,
            cy - BRAND_TOP_Y_OFFSET,
            cx + 48,
            cy - BRAND_TOP_Y_OFFSET + 24
        };
        lv_draw_label(draw_ctx, &tdsc, &la, "BUC", NULL);
    }

    /* ── Lower brand ─────────────────────────────────────────────────────── */
    tdsc.font = &lv_font_montserrat_14;
    {
        lv_area_t la = {
            cx - 84,
            cy + BRAND_BOTTOM_Y_OFFSET,
            cx + 84,
            cy + BRAND_BOTTOM_Y_OFFSET + 20
        };
        lv_draw_label(draw_ctx, &tdsc, &la, "ChaoticVolt", NULL);
    }

    /* ── Subtle side accent for composition ──────────────────────────────── */
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = CLR_ACCENT;
    ldsc.width = 1;
    ldsc.opa   = LV_OPA_60;
    lv_point_t p1 = { PANEL_DATE_X + PANEL_DATE_W + 34, 86 };
    lv_point_t p2 = { PANEL_DATE_X + PANEL_DATE_W + 34, 498 };
    lv_draw_line(draw_ctx, &ldsc, &p1, &p2);
}

static void position_ornament_objects(void)
{
    if (s_clock_bg_obj) {
        lv_obj_set_pos(s_clock_bg_obj, CLOCK_CENTER_X - CLOCK_R, CLOCK_CENTER_Y - CLOCK_R);
    }
    if (s_clock_obj) {
        lv_obj_set_pos(s_clock_obj, CLOCK_CENTER_X - CLOCK_R, CLOCK_CENTER_Y - CLOCK_R);
    }

    if (s_lbl_date) {
        lv_obj_set_width(s_lbl_date, PANEL_DATE_W);
        lv_obj_set_style_text_align(s_lbl_date, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(s_lbl_date, PANEL_DATE_X, PANEL_DATE_Y);
    }

    if (s_lbl_room_temp) {
        lv_obj_set_width(s_lbl_room_temp, PANEL_ROOM_W);
        lv_obj_set_style_text_align(s_lbl_room_temp, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(s_lbl_room_temp, PANEL_ROOM_X, PANEL_ROOM_TEMP_Y);
    }

    if (s_lbl_room_rh) {
        lv_obj_set_width(s_lbl_room_rh, PANEL_ROOM_W);
        lv_obj_set_style_text_align(s_lbl_room_rh, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(s_lbl_room_rh, PANEL_ROOM_X, PANEL_ROOM_RH_Y);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * LV_EVENT_DRAW_POST  –  clock hands (drawn after all children)
 * ───────────────────────────────────────────────────────────────────────────── */
static void draw_hands_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t      *obj      = lv_event_get_target(e);

    lv_area_t oa;
    lv_obj_get_coords(obj, &oa);
    int32_t cx = oa.x1 + CLOCK_R;
    int32_t cy = oa.y1 + CLOCK_R;

    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.round_start = 1;
    ldsc.round_end   = 1;

    /* Hour hand */
    ldsc.color = CLR_HAND;
    ldsc.width = 7;
    draw_hand(draw_ctx, &ldsc, cx, cy, s_angle_h, HAND_H_TAIL, HAND_H_TIP);

    /* Minute hand */
    ldsc.width = 5;
    draw_hand(draw_ctx, &ldsc, cx, cy, s_angle_m, HAND_M_TAIL, HAND_M_TIP);

    /* Second hand */
    ldsc.width = 2;
    draw_hand(draw_ctx, &ldsc, cx, cy, s_angle_s_drawn, HAND_S_TAIL, HAND_S_TIP);

    /* Centre hub */
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color     = CLR_HAND;
    rdsc.bg_opa       = LV_OPA_COVER;
    rdsc.radius       = LV_RADIUS_CIRCLE;
    rdsc.border_width = 0;
    rdsc.shadow_width = 0;
    lv_area_t hub = { cx - 7, cy - 7, cx + 7, cy + 7 };
    lv_draw_rect(draw_ctx, &rdsc, &hub);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Animation exec callback  –  called by lv_anim for the second-hand bounce
 * ───────────────────────────────────────────────────────────────────────────── */
static void anim_sec_cb(void *var, int32_t val)
{
    /* val is angle in milliradians */
    LV_UNUSED(var);

    float next = (float)val * 0.001f;
    invalidate_hand_pair(s_angle_s_drawn, next, HAND_S_TAIL, HAND_S_TIP, 2);
    s_angle_s_drawn = next;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * clock_ui_create
 * ───────────────────────────────────────────────────────────────────────────── */
void clock_ui_create(lv_obj_t *parent)
{
    s_parent = parent;

    /* ── Precompute tick endpoints (relative offsets from clock centre) ─── */
    for (int i = 0; i < 60; i++) {
        float a   = (float)i * (2.0f * (float)M_PI / 60.0f);
        float sa  = sinf(a), ca = cosf(a);
        int32_t r_out = TICK_OUT_R;
        int32_t r_in  = r_out - (i % 5 == 0 ? TICK_MAJ_LEN : TICK_MIN_LEN);
        s_tick_out[i].x = (lv_coord_t)( r_out * sa);
        s_tick_out[i].y = (lv_coord_t)(-r_out * ca);
        s_tick_in [i].x = (lv_coord_t)( r_in  * sa);
        s_tick_in [i].y = (lv_coord_t)(-r_in  * ca);
    }

    /* ── Precompute numeral box positions (relative to obj top-left) ────── */
    for (int i = 0; i < 12; i++) {
        /* angle: i=0 → 12 o'clock (−90°), increases clockwise              */
        float a  = ((float)(i * 30) - 90.0f) * ((float)M_PI / 180.0f);
        float nx = CLOCK_R + (float)NUM_R * cosf(a);
        float ny = CLOCK_R + (float)NUM_R * sinf(a);
        s_num_area[i].x1 = (lv_coord_t)(nx - NUM_BOX_W / 2);
        s_num_area[i].y1 = (lv_coord_t)(ny - NUM_BOX_H / 2);
        s_num_area[i].x2 = s_num_area[i].x1 + NUM_BOX_W;
        s_num_area[i].y2 = s_num_area[i].y1 + NUM_BOX_H;
    }

    /* ── Screen ─────────────────────────────────────────────────────────── */
    lv_obj_t *scr = parent ? parent : lv_scr_act();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* ── Clock object (500 × 500, centred) ──────────────────────────────── */
    s_clock_bg_obj = lv_obj_create(scr);
    lv_obj_set_size(s_clock_bg_obj, CLOCK_R * 2, CLOCK_R * 2);
    position_ornament_objects();
    lv_obj_set_style_bg_opa(s_clock_bg_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_clock_bg_obj, 0, 0);
    lv_obj_set_style_shadow_width(s_clock_bg_obj, 0, 0);
    lv_obj_set_style_pad_all(s_clock_bg_obj, 0, 0);
    lv_obj_set_style_clip_corner(s_clock_bg_obj, false, 0);
    lv_obj_clear_flag(s_clock_bg_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_clock_bg_obj, draw_bg_cb, LV_EVENT_DRAW_MAIN, NULL);

    s_clock_obj = lv_obj_create(scr);
    lv_obj_set_size(s_clock_obj, CLOCK_R * 2, CLOCK_R * 2);
    position_ornament_objects();
    lv_obj_set_style_bg_opa(s_clock_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_clock_obj, 0, 0);
    lv_obj_set_style_shadow_width(s_clock_obj, 0, 0);
    lv_obj_set_style_pad_all(s_clock_obj, 0, 0);
    lv_obj_set_style_clip_corner(s_clock_obj, false, 0);
    lv_obj_clear_flag(s_clock_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_clock_obj, draw_hands_cb, LV_EVENT_DRAW_POST, NULL);

    /* ── Left-side ornament text ─────────────────────────────────────────── */
    s_lbl_date = lv_label_create(scr);
    lv_label_set_text(s_lbl_date, "---");
    lv_obj_set_style_text_font(s_lbl_date, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_lbl_date, CLR_DATE, 0);
    lv_obj_set_style_text_line_space(s_lbl_date, 8, 0);

    s_lbl_room_temp = lv_label_create(scr);
    lv_label_set_text(s_lbl_room_temp, "21.4");
    lv_obj_set_style_text_font(s_lbl_room_temp, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_lbl_room_temp, CLR_DATE, 0);

    s_lbl_room_rh = lv_label_create(scr);
    lv_label_set_text(s_lbl_room_rh, "43% RH");
    lv_obj_set_style_text_font(s_lbl_room_rh, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_lbl_room_rh, CLR_SUBTLE, 0);

    panel_alert_create_storm(scr, 54, 48);

    position_ornament_objects();

    ESP_LOGI(TAG, "Clock UI created (custom draw, no lv_meter)");
}

void clock_ui_set_room_context(const char *room_name, float temp_c, int rh_pct)
{
    char temp_buf[24];
    char rh_buf[24];
    LV_UNUSED(room_name);

    snprintf(temp_buf, sizeof(temp_buf), "%.1f", temp_c);
    snprintf(rh_buf, sizeof(rh_buf), "%d%% RH", rh_pct);

    static char s_last_temp[24] = "";
    static char s_last_rh[24]   = "";

    if (s_lbl_room_temp && strcmp(temp_buf, s_last_temp) != 0) {
        lv_label_set_text(s_lbl_room_temp, temp_buf);
        strncpy(s_last_temp, temp_buf, sizeof(s_last_temp) - 1);
    }
    if (s_lbl_room_rh && strcmp(rh_buf, s_last_rh) != 0) {
        lv_label_set_text(s_lbl_room_rh, rh_buf);
        strncpy(s_last_rh, rh_buf, sizeof(s_last_rh) - 1);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * clock_ui_tick  –  called every second from the LVGL task
 * ───────────────────────────────────────────────────────────────────────────── */
void clock_ui_tick(void)
{
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);

    int h = t.tm_hour % 12;
    int m = t.tm_min;
    int s = t.tm_sec;

    const float TWO_PI = 2.0f * (float)M_PI;

    /* Hour: smooth — advances with minutes */
    float old_angle_h = s_angle_h;
    float old_angle_m = s_angle_m;
    float old_angle_s = s_angle_s_drawn;

    s_angle_h = TWO_PI * ((float)(h * 60 + m) / 720.0f);

    /* Minute */
    s_angle_m = TWO_PI * ((float)m / 60.0f);

    /* Second — target angle */
    float s_new = TWO_PI * ((float)s / 60.0f);

    static float s_prev = -1.0f;

    if (s_prev >= 0.0f && s_new > s_prev) {
        /* Station-clock overshoot-bounce animation */
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_del(s_clock_obj, anim_sec_cb);
        lv_anim_set_exec_cb(&a, anim_sec_cb);
        lv_anim_set_var(&a, s_clock_obj);
        lv_anim_set_time(&a, 180);
        lv_anim_set_values(&a,
            (int32_t)(s_prev * 1000.0f),
            (int32_t)(s_new  * 1000.0f));
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    } else {
        /* Wrap (59→0) or first call: snap immediately */
        s_angle_s_drawn = s_new;
        invalidate_hand_pair(old_angle_s, s_angle_s_drawn, HAND_S_TAIL, HAND_S_TIP, 2);
    }

    if (fabsf(s_angle_h - old_angle_h) > 0.0001f) {
        invalidate_hand_pair(old_angle_h, s_angle_h, HAND_H_TAIL, HAND_H_TIP, 7);
    }
    if (fabsf(s_angle_m - old_angle_m) > 0.0001f) {
        invalidate_hand_pair(old_angle_m, s_angle_m, HAND_M_TAIL, HAND_M_TIP, 5);
    }

    s_angle_s = s_new;
    s_prev    = s_new;

    /* Date string */
    char day[20], mon[20], buf[64];
    strftime(day, sizeof(day), "%A", &t);
    strftime(mon, sizeof(mon), "%B", &t);
    snprintf(buf, sizeof(buf), "%s\n%d %s",
             day, t.tm_mday, mon);
    lv_label_set_text(s_lbl_date, buf);
}
