/**
 * @file  ui_compass.c
 * @brief Wind compass -- clock-matched bezel, ticks and hand.
 *
 * All graphics via LVGL 9.2 draw API inside a custom draw-event handler.
 * Direction labels are lv_label children rendered on top.
 */

#include "ui_compass.h"
#include "buc_display.h"
#include "ui_theme.h"
#include <math.h>
#include <stdio.h>
#include <stdbool.h>

/* ── Compass rose geometry ──────────────────────────────────────────── */
#define CX              190       /* centre X within container */
#define CY              190       /* centre Y within container */

#define COMP_R          190
#define BEZEL_W          12
#define FACE_R         (COMP_R - BEZEL_W)
#define R_TICK_OUT     (FACE_R - 4)
#define R_LABEL         138

/* Tick lengths (inward from R_TICK_START) -- reduced set, every 15 deg */
#define TICK_CARDINAL    20       /* N / O / Z / W */
#define TICK_INTERCARD   14       /* NO / ZO / ZW / NW */
#define TICK_MINOR        8       /* remaining 15-degree positions */

/* ── Hand geometry -- aligned with clock hand language ─────────────── */
#define HAND_TIP        150
#define HAND_TAIL       -30
#define HAND_WIDTH        4
#define HUB_R             7

/* ── Container position (relative to page-right parent at x=300) ───── */
#define COMP_CX         250
#define COMP_CY         218
#define CONT_X          (COMP_CX - CX)
#define CONT_Y          (COMP_CY - CY)
#define CONT_W          (CX * 2)
#define CONT_H          (CY * 2)

/* ── Runtime state ──────────────────────────────────────────────────── */
static float     s_dir  = 225.0f;
static lv_obj_t *s_cont;
static lv_obj_t *s_lbl_deg;
static lv_obj_t *s_lbl_dirs[8];
static bool      s_connected = false;   /* start red at boot until WiFi up */

static lv_color_t col_bezel(void)       { return ui_theme_is_night_mode() ? lv_color_hex(0x141414) : lv_color_hex(0x1B1E24); }
static lv_color_t col_face(void)        { return ui_theme_is_night_mode() ? lv_color_hex(0x0A0A0A) : lv_color_hex(0xF1F0EB); }
static lv_color_t col_tick_major(void)  { return ui_theme_is_night_mode() ? lv_color_hex(0x7C7266) : lv_color_hex(0x46433E); }
static lv_color_t col_tick_minor(void)  { return ui_theme_is_night_mode() ? lv_color_hex(0x44413C) : lv_color_hex(0x9A9A96); }
static lv_color_t col_cardinal(void)    { return ui_theme_is_night_mode() ? lv_color_hex(0x93887B) : lv_color_hex(0x2F3238); }
static lv_color_t col_intercardinal(void){ return ui_theme_is_night_mode() ? lv_color_hex(0x6B6358) : lv_color_hex(0x808799); }
static lv_color_t col_degree(void)      { return ui_theme_is_night_mode() ? lv_color_hex(0x93887B) : lv_color_hex(0x808799); }
static lv_color_t col_hand_ok(void)     { return ui_theme_is_night_mode() ? lv_color_hex(0xA78958) : lv_color_hex(0xB79258); }
static lv_color_t col_hand_err(void)    { return ui_theme_is_night_mode() ? lv_color_hex(0xA04840) : lv_color_hex(0xC02828); }
static lv_color_t col_hub(void)         { return ui_theme_is_night_mode() ? lv_color_hex(0xA78958) : lv_color_hex(0xB79258); }

/* ── Polar -> pixel (0 deg = North, clockwise) ─────────────────────── */
static inline void pol(float deg, float r, float *x, float *y)
{
    float a = deg * (float)M_PI / 180.0f;
    *x = (float)CX + r * sinf(a);
    *y = (float)CY - r * cosf(a);
}

/* ── Draw-event callback ───────────────────────────────────────────── */
static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    if (!layer) return;

    lv_obj_t *obj = lv_event_get_target(e);
    lv_area_t ca;
    lv_obj_get_coords(obj, &ca);
    const int32_t ox = ca.x1;
    const int32_t oy = ca.y1;

    /* ── 1. Bezel + face ───────────────────────────────────────────── */
    {
        lv_draw_rect_dsc_t rdsc;
        lv_draw_rect_dsc_init(&rdsc);
        rdsc.bg_color = col_bezel();
        rdsc.bg_opa = LV_OPA_COVER;
        rdsc.radius = LV_RADIUS_CIRCLE;
        rdsc.border_width = 0;
        rdsc.shadow_width = 0;

        lv_area_t oa = {
            .x1 = ox + CX - COMP_R,
            .y1 = oy + CY - COMP_R,
            .x2 = ox + CX + COMP_R,
            .y2 = oy + CY + COMP_R,
        };
        lv_draw_rect(layer, &rdsc, &oa);

        rdsc.bg_color = col_face();
        lv_area_t fa = {
            .x1 = oa.x1 + BEZEL_W,
            .y1 = oa.y1 + BEZEL_W,
            .x2 = oa.x2 - BEZEL_W,
            .y2 = oa.y2 - BEZEL_W,
        };
        lv_draw_rect(layer, &rdsc, &fa);
    }

    /* ── 2. Tick marks -- every 15 deg (24 ticks, was 36 at 10 deg) ─ */
    for (int i = 0; i < 24; i++) {
        float deg = (float)(i * 15);
        int   ideg = i * 15;
        int   len, w;
        lv_color_t col;

        if      (ideg % 90 == 0) { len = TICK_CARDINAL;  w = 3; col = col_tick_major(); }
        else if (ideg % 45 == 0) { len = TICK_INTERCARD;  w = 2; col = col_tick_major(); }
        else                     { len = TICK_MINOR;       w = 2; col = col_tick_minor(); }

        float x1, y1, x2, y2;
        pol(deg, (float)R_TICK_OUT, &x1, &y1);
        pol(deg, (float)(R_TICK_OUT - len), &x2, &y2);

        lv_draw_line_dsc_t ln;
        lv_draw_line_dsc_init(&ln);
        ln.p1.x       = (lv_value_precise_t)x1 + ox;
        ln.p1.y       = (lv_value_precise_t)y1 + oy;
        ln.p2.x       = (lv_value_precise_t)x2 + ox;
        ln.p2.y       = (lv_value_precise_t)y2 + oy;
        ln.color       = col;
        ln.width       = w;
        ln.round_start = 1;
        ln.round_end   = 1;
        lv_draw_line(layer, &ln);
    }

    /* ── 3. Hand ───────────────────────────────────────────────────── */
    float rad = s_dir * (float)M_PI / 180.0f;
    float sn  = sinf(rad);
    float cs  = cosf(rad);

    float tip_x  = (float)(ox + CX) + (float)HAND_TIP * sn;
    float tip_y  = (float)(oy + CY) - (float)HAND_TIP * cs;
    float tail_x = (float)(ox + CX) + (float)HAND_TAIL * sn;
    float tail_y = (float)(oy + CY) - (float)HAND_TAIL * cs;

    {
        lv_draw_line_dsc_t ndsc;
        lv_draw_line_dsc_init(&ndsc);
        ndsc.color = s_connected ? col_hand_ok() : col_hand_err();
        ndsc.width = HAND_WIDTH;
        ndsc.round_start = 1;
        ndsc.round_end = 1;
        ndsc.p1.x = (lv_value_precise_t)tail_x;
        ndsc.p1.y = (lv_value_precise_t)tail_y;
        ndsc.p2.x = (lv_value_precise_t)tip_x;
        ndsc.p2.y = (lv_value_precise_t)tip_y;
        lv_draw_line(layer, &ndsc);
    }

    /* ── 4. Hub ────────────────────────────────────────────────────── */
    {
        lv_draw_rect_dsc_t hub;
        lv_draw_rect_dsc_init(&hub);
        hub.bg_color = col_hub();
        hub.bg_opa = LV_OPA_COVER;
        hub.radius = LV_RADIUS_CIRCLE;
        lv_area_t hub_area = {
            .x1 = ox + CX - HUB_R,
            .y1 = oy + CY - HUB_R,
            .x2 = ox + CX + HUB_R,
            .y2 = oy + CY + HUB_R,
        };
        lv_draw_rect(layer, &hub, &hub_area);
    }

}

/* ── Public: create compass widget ─────────────────────────────────── */

void ui_compass_create(lv_obj_t *parent)
{
    /* Transparent container -- all graphics via draw event callback */
    s_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(s_cont);
    lv_obj_set_pos(s_cont, CONT_X, CONT_Y);
    lv_obj_set_size(s_cont, CONT_W, CONT_H);
    lv_obj_clear_flag(s_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cont, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_cont, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_cont, draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    /* ── Direction labels (children, rendered on top of draw layer) ── */
    static const struct {
        const char      *text;
        float            deg;
        const lv_font_t *font;
        int              cardinal;   /* 1 = N/O/Z/W, 0 = inter-cardinal */
    } dirs[] = {
        { "N",   0.0f,   &lv_font_montserrat_20, 1 },
        { "NO",  45.0f,  &lv_font_montserrat_14, 0 },
        { "O",   90.0f,  &lv_font_montserrat_20, 1 },
        { "ZO", 135.0f,  &lv_font_montserrat_14, 0 },
        { "Z",  180.0f,  &lv_font_montserrat_20, 1 },
        { "ZW", 225.0f,  &lv_font_montserrat_14, 0 },
        { "W",  270.0f,  &lv_font_montserrat_20, 1 },
        { "NW", 315.0f,  &lv_font_montserrat_14, 0 },
    };

    for (int i = 0; i < 8; i++) {
        float lx, ly;
        pol(dirs[i].deg, (float)R_LABEL, &lx, &ly);

        lv_obj_t *lbl = lv_label_create(s_cont);
        s_lbl_dirs[i] = lbl;
        lv_label_set_text(lbl, dirs[i].text);
        lv_obj_set_style_text_font(lbl, dirs[i].font, 0);
        lv_obj_set_style_text_color(lbl,
            dirs[i].cardinal ? col_cardinal() : col_intercardinal(), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);

        /* Centre each label on its polar coordinate */
        lv_obj_update_layout(lbl);
        lv_obj_set_pos(lbl,
                        (int32_t)lx - lv_obj_get_width(lbl) / 2,
                        (int32_t)ly - lv_obj_get_height(lbl) / 2);
    }

    /* ── Degree readout inside the rose, aligned with the clock palette ─ */
    s_lbl_deg = lv_label_create(s_cont);
    lv_obj_set_style_text_font(s_lbl_deg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_deg, col_degree(), 0);
    lv_obj_set_style_text_align(s_lbl_deg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_deg, 64);
    lv_label_set_text(s_lbl_deg, "225\u00B0");
    lv_obj_set_pos(s_lbl_deg, CX - 32, CY + 118);

    ui_compass_set_direction(225.0f);
}

/* ── Public: update needle direction ───────────────────────────────── */

void ui_compass_set_direction(float deg)
{
    s_dir = deg;
    if (s_cont == NULL || s_lbl_deg == NULL) return;
    lv_obj_invalidate(s_cont);

    /* C snprintf -- LVGL's lv_snprintf doesn't support %f */
    char buf[12];
    snprintf(buf, sizeof(buf), "%.0f\u00B0", (double)deg);
    lv_label_set_text(s_lbl_deg, buf);
}

/* ── Public: content/connectivity state ────────────────────────────── */

void ui_compass_set_connected(bool connected)
{
    if (s_connected == connected) return;
    s_connected = connected;
    if (s_cont) lv_obj_invalidate(s_cont);
}

void ui_compass_apply_theme(void)
{
    if (s_cont == NULL) {
        return;
    }

    for (int i = 0; i < 8; i++) {
        if (s_lbl_dirs[i] == NULL) continue;
        bool cardinal = (i % 2 == 0);
        lv_obj_set_style_text_color(s_lbl_dirs[i],
            cardinal ? col_cardinal() : col_intercardinal(), 0);
    }
    if (s_lbl_deg) {
        lv_obj_set_style_text_color(s_lbl_deg, col_degree(), 0);
    }
    lv_obj_invalidate(s_cont);
}
