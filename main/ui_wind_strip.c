/**
 * @file  ui_wind_strip.c
 * @brief Horizontal wind/gust bar -- Beaufort scale 0-12.
 *
 * Gradient-based visualisation: the full track shows a left-to-right
 * wind-severity spectrum (cyan -> green -> yellow -> orange -> red ->
 * magenta).  Three brightness zones communicate state at a glance:
 *
 *   Active wind  (0 to wind_bft)       : full-colour gradient
 *   Gust reach   (wind_bft to gust_bft): medium brightness
 *   Inactive     (gust_bft to 12)      : very dim
 *
 * A strong white marker indicates the steady-wind position.
 * A subtle vertical line marks the gust limit.
 *
 * All rendering is native LVGL (lv_draw_rect, lv_draw_line) in a
 * custom draw-event callback.  Labels are LVGL label objects.
 */

#include "ui_wind_strip.h"
#include "buc_display.h"
#include <math.h>
#include <stdio.h>
#include <stdbool.h>

/* ── Layout constants ──────────────────────────────────────────────── */
#define STRIP_X        80        /* relative to page-right parent (was 380, minus 300) */
#define STRIP_Y       390        /* top edge (y unchanged) */
#define STRIP_W       390        /* total width */
#define STRIP_H        55        /* total height */

#define TRACK_H        14        /* gradient bar thickness */
#define TRACK_Y        20        /* bar vertical offset within container */
#define TRACK_PAD       2        /* left/right inset */

#define BFT_MAX      12.0f       /* Beaufort scale maximum */
#define MARKER_W        3        /* steady-wind marker width */
#define MARKER_H       22        /* steady-wind marker height */

#define NUM_SEGMENTS   48        /* number of coloured rects in gradient */

/* ── Gradient colour stops (Beaufort severity spectrum) ────────────── */
typedef struct { float bft; uint8_t r, g, b; } grad_stop_t;

static const grad_stop_t GRAD[] = {
    {  0.0f, 0x20, 0xB0, 0xE8 },   /* cyan         (calm)       */
    {  2.0f, 0x30, 0xC8, 0x70 },   /* green        (light)      */
    {  4.0f, 0xA0, 0xD0, 0x30 },   /* yellow-green (moderate)   */
    {  6.0f, 0xE8, 0xC0, 0x20 },   /* yellow       (fresh)      */
    {  8.0f, 0xF0, 0x70, 0x20 },   /* orange       (strong)     */
    { 10.0f, 0xE8, 0x20, 0x20 },   /* red          (gale)       */
    { 12.0f, 0xD0, 0x30, 0xA0 },   /* magenta      (storm+)     */
};
#define GRAD_STOPS  ((int)(sizeof(GRAD) / sizeof(GRAD[0])))

/* ── Zone brightness multipliers ───────────────────────────────────── */
#define BRIGHT_WIND    1.00f     /* active wind: full colour */
#define BRIGHT_GUST    0.40f     /* gust overshoot: dimmed */
#define BRIGHT_IDLE    0.10f     /* inactive portion: very dim */

/* ── Runtime state ─────────────────────────────────────────────────── */
static lv_obj_t *s_cont;
static lv_obj_t *s_lbl_wind;            /* wind value above the marker */
static float     s_wind_bft = 3.0f;
static float     s_gust_bft = 5.0f;
static bool      s_connected = false;   /* start red at boot until WiFi up */

#define WIND_LBL_W   40                 /* wind value label width (centered) */

/* Marker / label colours for connectivity states */
#define COL_MARKER_OK    lv_color_hex(0xFFFFFF)  /* white: transport ok   */
#define COL_MARKER_ERR   lv_color_hex(0xE02828)  /* red:   no connectivity */

/* ── Interpolate gradient colour at a Beaufort value ───────────────── */
static lv_color_t grad_color(float bft)
{
    if (bft <= 0.0f) bft = 0.0f;
    if (bft >= BFT_MAX) bft = BFT_MAX;

    /* Find the gradient segment this value falls into */
    int seg = 0;
    for (int i = 0; i < GRAD_STOPS - 2; i++) {
        if (bft >= GRAD[i + 1].bft) seg = i + 1;
        else break;
    }

    float span = GRAD[seg + 1].bft - GRAD[seg].bft;
    float t = (span > 0.0f) ? (bft - GRAD[seg].bft) / span : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    return lv_color_make(
        (uint8_t)((int)GRAD[seg].r + (int)((int)GRAD[seg + 1].r - (int)GRAD[seg].r) * t),
        (uint8_t)((int)GRAD[seg].g + (int)((int)GRAD[seg + 1].g - (int)GRAD[seg].g) * t),
        (uint8_t)((int)GRAD[seg].b + (int)((int)GRAD[seg + 1].b - (int)GRAD[seg].b) * t)
    );
}

/* ── Dim a colour by a brightness factor (0.0-1.0) ────────────────── */
static lv_color_t dim_color(lv_color_t c, float bright)
{
    return lv_color_make(
        (uint8_t)(c.red   * bright),
        (uint8_t)(c.green * bright),
        (uint8_t)(c.blue  * bright)
    );
}

/* ── Draw-event callback ───────────────────────────────────────────── */
/*
 * Draw order:
 *   gradient segments -> gust-end line -> wind marker
 */
static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    if (!layer) return;

    lv_obj_t *obj = lv_event_get_target(e);
    lv_area_t ca;
    lv_obj_get_coords(obj, &ca);
    const int32_t ox = ca.x1;
    const int32_t oy = ca.y1;

    const float track_w = (float)(STRIP_W - 2 * TRACK_PAD);
    const float wind_frac = s_wind_bft / BFT_MAX;
    const float gust_frac = s_gust_bft / BFT_MAX;

    /* ── 1. Gradient segments ──────────────────────────────────────── */
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        float frac_mid = ((float)i + 0.5f) / (float)NUM_SEGMENTS;
        float bft = frac_mid * BFT_MAX;

        /* Zone brightness */
        float bright;
        if (frac_mid <= wind_frac)
            bright = BRIGHT_WIND;
        else if (frac_mid <= gust_frac)
            bright = BRIGHT_GUST;
        else
            bright = BRIGHT_IDLE;

        lv_color_t col = dim_color(grad_color(bft), bright);

        int32_t x1 = ox + TRACK_PAD + (int32_t)((float)i / (float)NUM_SEGMENTS * track_w);
        int32_t x2 = ox + TRACK_PAD + (int32_t)((float)(i + 1) / (float)NUM_SEGMENTS * track_w) - 1;
        if (x2 < x1) x2 = x1;

        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = col;
        d.bg_opa   = LV_OPA_COVER;
        d.radius   = 0;

        lv_area_t a;
        a.x1 = x1;
        a.y1 = oy + TRACK_Y;
        a.x2 = x2;
        a.y2 = oy + TRACK_Y + TRACK_H - 1;
        lv_draw_rect(layer, &d, &a);
    }

    /* ── 2. Gust-end line (subtle vertical tick) ───────────────────── */
    {
        int32_t gx = ox + TRACK_PAD + (int32_t)(gust_frac * track_w);

        lv_draw_line_dsc_t ln;
        lv_draw_line_dsc_init(&ln);
        ln.color = lv_color_hex(0xFFFFFF);
        ln.width = 1;
        ln.opa   = LV_OPA_40;
        ln.p1.x  = gx;
        ln.p1.y  = oy + TRACK_Y - 1;
        ln.p2.x  = gx;
        ln.p2.y  = oy + TRACK_Y + TRACK_H;
        lv_draw_line(layer, &ln);
    }

    /* ── 3. Wind marker (strong vertical bar, colour = transport state) ── */
    {
        int32_t mx = ox + TRACK_PAD + (int32_t)(wind_frac * track_w);

        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = s_connected ? COL_MARKER_OK : COL_MARKER_ERR;
        d.bg_opa   = LV_OPA_COVER;
        d.radius   = 1;

        lv_area_t a;
        a.x1 = mx - MARKER_W / 2;
        a.y1 = oy + TRACK_Y - (MARKER_H - TRACK_H) / 2;
        a.x2 = mx + MARKER_W / 2;
        a.y2 = oy + TRACK_Y - (MARKER_H - TRACK_H) / 2 + MARKER_H - 1;
        lv_draw_rect(layer, &d, &a);
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

void ui_wind_strip_create(lv_obj_t *parent)
{
    /* Container */
    s_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(s_cont);
    lv_obj_set_pos(s_cont, STRIP_X, STRIP_Y);
    lv_obj_set_size(s_cont, STRIP_W, STRIP_H);
    lv_obj_clear_flag(s_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_cont, draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    /* ── Wind value label above the marker (colour follows transport) ─ */
    s_lbl_wind = lv_label_create(s_cont);
    lv_label_set_text(s_lbl_wind, "3");
    lv_obj_set_style_text_font(s_lbl_wind, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_wind,
        s_connected ? COL_MARKER_OK : COL_MARKER_ERR, 0);
    lv_obj_set_style_text_align(s_lbl_wind, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_wind, WIND_LBL_W);

    /* Initial draw with default values */
    ui_wind_strip_update(s_wind_bft, s_gust_bft);
}

void ui_wind_strip_update(float wind_bft, float gust_bft)
{
    s_wind_bft = wind_bft;
    s_gust_bft = gust_bft;

    /* Update wind value label text and position (centered above marker) */
    char buf[8];
    snprintf(buf, sizeof(buf), "%.0f", (double)wind_bft);
    lv_label_set_text(s_lbl_wind, buf);

    const float track_w  = (float)(STRIP_W - 2 * TRACK_PAD);
    const float wind_frac = wind_bft / BFT_MAX;
    int32_t marker_x = TRACK_PAD + (int32_t)(wind_frac * track_w);
    int32_t lbl_x    = marker_x - WIND_LBL_W / 2;
    if (lbl_x < 0) lbl_x = 0;
    if (lbl_x > STRIP_W - WIND_LBL_W) lbl_x = STRIP_W - WIND_LBL_W;
    lv_obj_set_pos(s_lbl_wind, lbl_x, 0);

    lv_obj_invalidate(s_cont);
}

/* ── Public: connectivity state ────────────────────────────────────── */

void ui_wind_strip_set_connected(bool connected)
{
    if (s_connected == connected) return;
    s_connected = connected;
    if (s_lbl_wind) {
        lv_obj_set_style_text_color(s_lbl_wind,
            s_connected ? COL_MARKER_OK : COL_MARKER_ERR, 0);
    }
    if (s_cont) lv_obj_invalidate(s_cont);
}
