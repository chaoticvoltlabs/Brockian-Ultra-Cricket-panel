/**
 * @file  ui_weather.c
 * @brief Left-column weather panel -- stacked lines, no card background.
 *
 * Matches the proven panel style from the reference display:
 *   text directly on the dark screen background, no card or border.
 *   Stacked single-line items with clear hierarchy.
 *   Orange accent on Bft values only.
 *
 * Layout (transparent column at x:30, y:28):
 *   - Hero temperature              (Montserrat 48, white)
 *     Pages 0-1: outdoor temp, Page 2: local room temp
 *   - Secondary line                (Montserrat 20, medium grey)
 *     Pages 0-1: "Feels Like X°C", Page 2: "XX% RH"
 *   - [pages 0-1] Outdoor RH%      (Montserrat 20, medium grey)
 *   - divider
 *   - [page 2 only] Clock HH:MM    (Montserrat 28, light grey, NTP)
 *   - [page 2 only] divider
 *   - Wind label + Bft value + km/h
 *   - Gust label + Bft value
 *   - divider
 *   - [pages 0-1] Clock HH:MM      (Montserrat 28, light grey, NTP)
 *   - [pages 0-1] divider
 *   - Pressure caption + 24h trend line graph
 *
 * Barometric trend: 60-point circular buffer drawn as connected
 * grey line segments in a custom draw callback.  Auto-scales with
 * a minimum 6 hPa range.  No axes, no grid, no decoration.
 */

#include "ui_weather.h"
#include "buc_display.h"
#include "ui_theme.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

/* ── Column layout ─────────────────────────────────────────────────── */
#define COL_X           30        /* left edge on screen */
#define COL_Y           28        /* top edge on screen */
#define COL_W          270        /* column container width */
#define COL_H          440        /* tall enough for all content */

/* Visual content width -- matches the divider lines (55% of COL_W).
 * The graph is constrained to this so it aligns with the divider rules
 * and never sticks out past the perceived column edge. */
#define CONTENT_W      ((COL_W * 55) / 100)   /* ≈148 px */
#define CLOCK_CONTENT_W ((COL_W * 49) / 100)  /* slightly tighter clock page */
#define ALERT_X        314
#define ALERT_Y         36
#define ALERT_SIZE      28

/* ── Barometric trend ──────────────────────────────────────────────── */
#define BARO_HISTORY    60        /* readings in circular buffer */
#define GRAPH_H         36        /* graph container height px */
#define BARO_MIN_RANGE   6.0f     /* minimum visible hPa range */

/* ── Handles for runtime updates ───────────────────────────────────── */
static lv_obj_t *lbl_temp;
static lv_obj_t *lbl_feel;
static lv_obj_t *lbl_hum_out;     /* outdoor RH line (pages 0-1 only)    */
static lv_obj_t *lbl_clock;       /* clock between secondary & wind (p2) */
static lv_obj_t *lbl_clock_alt;   /* clock in spacer zone (pages 0-1)    */
static lv_obj_t *lbl_date_info;   /* weekday + date (clock page only)    */
static lv_obj_t *lbl_room_temp;   /* local room temp (clock page only)   */
static lv_obj_t *lbl_trend_caption;
static lv_obj_t *lbl_wind_title;
static lv_obj_t *s_wind_row;
static lv_obj_t *lbl_wind_main;
static lv_obj_t *lbl_wind_bft;
static lv_obj_t *lbl_wind_sep;
static lv_obj_t *lbl_wind_gust;
static lv_obj_t *lbl_wind_kmh;
static lv_obj_t *lbl_gust_title;
static lv_obj_t *lbl_gust_bft;
static lv_obj_t *lbl_press_val;
static lv_obj_t *s_graph;
static lv_obj_t *s_storm_alert;

/* Elements that swap visibility / margin between pages 0-1 and page 2 */
static lv_obj_t *s_div1;       /* divider after secondary / humidity    */
static lv_obj_t *s_div_clock;  /* divider after clock (page 2 only)    */
static lv_obj_t *s_div2;       /* divider after gust                   */
static lv_obj_t *s_div_room;   /* divider above room temp (clock page) */
static lv_obj_t *s_div3;       /* divider before pressure (pages 0-1)  */
static lv_obj_t *s_clock_gap;  /* flex spacer for clock page           */

/* Pressure history circular buffer */
static float s_baro[BARO_HISTORY];
static int   s_baro_idx   = 0;
static int   s_baro_count = 0;

/* Page-aware hero + secondary (outdoor vs room climate) */
static int   s_page         = 0;
static float s_outdoor_temp = 0;
static float s_feel_temp    = 0;
static float s_outdoor_hum  = 0;
static float s_room_temp    = 0;
static bool  s_room_temp_ok = false;
static float s_room_rh      = 0;
static bool  s_room_rh_ok   = false;
static float s_wind_bft     = 3.0f;
static float s_gust_bft     = 5.0f;
static bool  s_storm_active = false;

static lv_color_t col_hero(void)
{
    return ui_theme_is_night_mode() ? lv_color_hex(0xC9BCA5) : COL_WHITE;
}

static lv_color_t col_text_secondary(void)
{
    return ui_theme_is_night_mode() ? lv_color_hex(0x8B8172) : COL_LIGHT_GREY;
}

static lv_color_t col_text_muted(void)
{
    return ui_theme_is_night_mode() ? lv_color_hex(0x615A50) : COL_MED_GREY;
}

static lv_color_t col_separator(void)
{
    return ui_theme_is_night_mode() ? lv_color_hex(0x181410) : COL_SEPARATOR;
}

static lv_color_t col_graph_line(void)
{
    return ui_theme_is_night_mode() ? lv_color_hex(0x756D62) : lv_color_hex(0xB0B8C8);
}

static lv_color_t col_alert_idle(void)
{
    return ui_theme_is_night_mode() ? lv_color_hex(0x3A342D) : lv_color_hex(0x67615A);
}

static lv_color_t col_alert_on(void)
{
    return ui_theme_is_night_mode() ? lv_color_hex(0xD8A23E) : lv_color_hex(0xD39A30);
}

static lv_color_t theme_bft_color(float bft);

/* ── Helpers ───────────────────────────────────────────────────────── */

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_color_t color, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

static lv_obj_t *make_separator(lv_obj_t *parent, int32_t mb)
{
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_remove_style_all(sep);
    lv_obj_add_flag(sep, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(sep, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(sep, lv_pct(55), 1);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sep, col_separator(), 0);
    lv_obj_set_style_margin_bottom(sep, mb, 0);
    return sep;
}

static void storm_blink_cb(void *var, int32_t value)
{
    lv_obj_t *obj = var;
    if (obj == NULL) return;
    lv_obj_set_style_text_opa(obj, (lv_opa_t)value, 0);
}

static void storm_alert_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    if (!layer) return;

    lv_obj_t *obj = lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    int32_t cx = (a.x1 + a.x2) / 2;
    int32_t top = a.y1 + 1;
    int32_t bottom = a.y2 - 1;

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_obj_get_style_text_color(obj, 0);
    line.opa = lv_obj_get_style_text_opa(obj, 0);
    line.width = 3;
    line.round_start = 1;
    line.round_end = 1;

    line.p1.x = cx;      line.p1.y = top;
    line.p2.x = a.x1+2;  line.p2.y = bottom;
    lv_draw_line(layer, &line);
    line.p1.x = a.x1+2;  line.p1.y = bottom;
    line.p2.x = a.x2-2;  line.p2.y = bottom;
    lv_draw_line(layer, &line);
    line.p1.x = a.x2-2;  line.p1.y = bottom;
    line.p2.x = cx;      line.p2.y = top;
    lv_draw_line(layer, &line);

    lv_draw_rect_dsc_t mark;
    lv_draw_rect_dsc_init(&mark);
    mark.bg_color = line.color;
    mark.bg_opa = line.opa;
    mark.border_width = 0;
    mark.radius = LV_RADIUS_CIRCLE;

    lv_area_t stem = { cx - 1, a.y1 + 8, cx + 1, a.y1 + 16 };
    lv_draw_rect(layer, &mark, &stem);

    lv_area_t dot = { cx - 2, a.y1 + 20, cx + 2, a.y1 + 24 };
    lv_draw_rect(layer, &mark, &dot);
}

static void apply_storm_alert_style(void)
{
    if (s_storm_alert == NULL) return;

    lv_anim_del(s_storm_alert, storm_blink_cb);

    if (s_storm_active) {
        lv_obj_set_style_text_color(s_storm_alert, col_alert_on(), 0);
        lv_obj_set_style_text_opa(s_storm_alert, LV_OPA_30, 0);

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, s_storm_alert);
        lv_anim_set_exec_cb(&anim, storm_blink_cb);
        lv_anim_set_values(&anim, LV_OPA_30, LV_OPA_COVER);
        lv_anim_set_time(&anim, 1300);
        lv_anim_set_playback_time(&anim, 1300);
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&anim);
    } else {
        lv_obj_set_style_text_color(s_storm_alert, col_alert_idle(), 0);
        lv_obj_set_style_text_opa(s_storm_alert, LV_OPA_30, 0);
    }

    lv_obj_invalidate(s_storm_alert);
}

/** Beaufort to approximate km/h. */
static int bft_to_kmh(float bft)
{
    if (bft <= 0.0f) return 0;
    return (int)(3.01f * powf(bft, 1.5f) + 0.5f);
}

/* ── Gradient colour for Bft value (same spectrum as wind strip) ───── */
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

static lv_color_t theme_bft_color(float bft)
{
    if (bft <= 0.0f) bft = 0.0f;
    if (bft >= 12.0f) bft = 12.0f;

    int seg = 0;
    for (int i = 0; i < GRAD_STOPS - 2; i++) {
        if (bft >= GRAD[i + 1].bft) seg = i + 1;
        else break;
    }

    float span = GRAD[seg + 1].bft - GRAD[seg].bft;
    float t = (span > 0.0f) ? (bft - GRAD[seg].bft) / span : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    lv_color_t base = lv_color_make(
        (uint8_t)((int)GRAD[seg].r + (int)((int)GRAD[seg + 1].r - (int)GRAD[seg].r) * t),
        (uint8_t)((int)GRAD[seg].g + (int)((int)GRAD[seg + 1].g - (int)GRAD[seg].g) * t),
        (uint8_t)((int)GRAD[seg].b + (int)((int)GRAD[seg + 1].b - (int)GRAD[seg].b) * t)
    );
    if (!ui_theme_is_night_mode()) {
        return base;
    }
    return lv_color_mix(base, lv_color_hex(0x000000), 96);
}

/* ── Barometric trend draw callback ────────────────────────────────── */

static void graph_draw_cb(lv_event_t *e)
{
    if (s_baro_count < 2) return;

    lv_layer_t *layer = lv_event_get_layer(e);
    if (!layer) return;

    lv_obj_t *obj = lv_event_get_target(e);
    lv_area_t ca;
    lv_obj_get_coords(obj, &ca);

    /* Use the object's actual width so page-specific sizing stays aligned. */
    const int32_t gx = ca.x1;
    const int32_t gy = ca.y1;
    const int32_t gw = lv_area_get_width(&ca);
    const int32_t gh = lv_area_get_height(&ca);

    /* Find min/max pressure in buffer */
    float pmin = 1100.0f, pmax = 900.0f;
    for (int i = 0; i < s_baro_count; i++) {
        if (s_baro[i] < pmin) pmin = s_baro[i];
        if (s_baro[i] > pmax) pmax = s_baro[i];
    }

    /* Enforce minimum vertical range to avoid noise amplification */
    float range = pmax - pmin;
    if (range < BARO_MIN_RANGE) {
        float mid = (pmin + pmax) * 0.5f;
        pmin = mid - BARO_MIN_RANGE * 0.5f;
        pmax = mid + BARO_MIN_RANGE * 0.5f;
        range = BARO_MIN_RANGE;
    }

    const int32_t margin = 3;
    const float   plot_h = (float)(gh - 2 * margin);

    int start = (s_baro_count < BARO_HISTORY) ? 0 : s_baro_idx;

    lv_draw_line_dsc_t ln;
    lv_draw_line_dsc_init(&ln);
    ln.color       = col_graph_line();
    ln.width       = 1;
    ln.opa         = LV_OPA_COVER;
    ln.round_start = 0;
    ln.round_end   = 0;

    for (int i = 0; i < s_baro_count - 1; i++) {
        int idx0 = (start + i)     % BARO_HISTORY;
        int idx1 = (start + i + 1) % BARO_HISTORY;

        float x0 = (float)i       / (float)(s_baro_count - 1) * (float)(gw - 1);
        float x1 = (float)(i + 1) / (float)(s_baro_count - 1) * (float)(gw - 1);

        float y0 = (float)margin + (1.0f - (s_baro[idx0] - pmin) / range) * plot_h;
        float y1 = (float)margin + (1.0f - (s_baro[idx1] - pmin) / range) * plot_h;

        ln.p1.x = gx + (int32_t)x0;
        ln.p1.y = gy + (int32_t)y0;
        ln.p2.x = gx + (int32_t)x1;
        ln.p2.y = gy + (int32_t)y1;
        lv_draw_line(layer, &ln);
    }
}

/* ── Page-aware hero temperature (outdoor vs room) ────────────────── */

static void refresh_hero(void)
{
    char buf[32];
    if (s_page == 2) {
        if (s_room_temp_ok)
            snprintf(buf, sizeof(buf), "%.1f\u00B0C", (double)s_room_temp);
        else
            snprintf(buf, sizeof(buf), "--.-\u00B0C");
    } else {
        if (s_page == 1)
            snprintf(buf, sizeof(buf), "%.1f\u00B0", (double)s_outdoor_temp);
        else
            snprintf(buf, sizeof(buf), "%.1f\u00B0C", (double)s_outdoor_temp);
    }
    lv_label_set_text(lbl_temp, buf);
}

/* ── Secondary line (Feels vs room RH depending on page) ────────── */

static void refresh_secondary(void)
{
    char buf[32];
    if (s_page == 2) {
        if (s_room_rh_ok)
            snprintf(buf, sizeof(buf), "%.0f%% RH", (double)s_room_rh);
        else
            snprintf(buf, sizeof(buf), "--%%  RH");
        lv_label_set_text(lbl_feel, buf);
    } else {
        snprintf(buf, sizeof(buf), "Feels %.0f\u00B0C", (double)s_feel_temp);
        lv_label_set_text(lbl_feel, buf);
    }
}

static void refresh_room_temp_line(void)
{
    char buf[32];
    if (s_room_temp_ok)
        snprintf(buf, sizeof(buf), s_page == 1 ? "%.1f\u00B0" : "%.1f\u00B0C", (double)s_room_temp);
    else
        snprintf(buf, sizeof(buf), s_page == 1 ? "--.-\u00B0" : "--.-\u00B0C");
    lv_label_set_text(lbl_room_temp, buf);
}

/* ── Page-aware column layout ─────────────────────────────────────── */
/*
 * Pages 0-1 (weather): original layout with spacer (future date/time).
 * Page 2 (controls):   clock visible, spacer removed, tighter spacing.
 * Hidden flex children don't participate in layout, so the column
 * reflowed automatically when we toggle visibility.
 */
static void apply_page_layout(void)
{
    if (s_page == 2) {
        /* Control page: room context — clock between hero/wind, no humidity line */
        lv_obj_remove_flag(lbl_feel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_hum_out, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_div_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_clock_alt, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_date_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_room_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_div_room, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_div3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_clock_gap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_flex_grow(s_clock_gap, 0);
        lv_obj_remove_flag(lbl_wind_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_wind_kmh, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_gust_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_gust_bft, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_font(lbl_hum_out, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_font(lbl_wind_main, &lv_font_montserrat_36, 0);
        lv_obj_set_style_pad_bottom(lbl_feel, 14, 0);
        lv_obj_set_style_margin_bottom(s_div1, 12, 0);
        lv_obj_set_style_margin_bottom(s_div2, 16, 0);
    } else if (s_page == 1) {
        /* Clock page: compact weather summary + date + room temperature */
        lv_obj_add_flag(lbl_feel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_hum_out, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_div_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_clock_alt, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_date_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_room_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_div_room, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_div3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_clock_gap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_flex_grow(s_clock_gap, 1);
        lv_obj_add_flag(lbl_wind_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_wind_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_wind_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_wind_kmh, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_gust_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_gust_bft, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_div1, CLOCK_CONTENT_W);
        lv_obj_set_width(s_div2, CLOCK_CONTENT_W);
        lv_obj_set_width(s_div_room, CLOCK_CONTENT_W);
        lv_obj_set_width(s_div3, CLOCK_CONTENT_W);
        lv_obj_set_width(s_wind_row, CLOCK_CONTENT_W);
        lv_obj_set_width(lbl_date_info, CLOCK_CONTENT_W);
        lv_obj_set_width(s_graph, CLOCK_CONTENT_W);
        lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_font(lbl_hum_out, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_font(lbl_wind_bft, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_font(lbl_wind_sep, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_font(lbl_wind_gust, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_font(lbl_date_info, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_font(lbl_room_temp, &lv_font_montserrat_28, 0);
        lv_obj_set_style_pad_bottom(lbl_temp, 8, 0);
        lv_obj_set_style_pad_bottom(lbl_hum_out, 16, 0);
        lv_obj_set_style_margin_bottom(s_wind_row, 8, 0);
        lv_obj_set_style_margin_bottom(s_div1, 16, 0);
        lv_obj_set_style_margin_bottom(s_div2, 14, 0);
        lv_obj_set_style_margin_bottom(s_div_room, 14, 0);
        lv_obj_set_style_margin_bottom(s_div3, 14, 0);
        lv_obj_set_style_pad_bottom(lbl_press_val, 4, 0);
        lv_obj_set_style_pad_bottom(s_graph, 2, 0);
        lv_obj_set_style_pad_bottom(lbl_trend_caption, 0, 0);
        lv_obj_set_style_pad_bottom(lbl_date_info, 16, 0);
        lv_obj_set_style_pad_bottom(lbl_room_temp, 8, 0);
        lv_obj_set_style_pad_bottom(lbl_feel, 0, 0);
    } else {
        /* Page 0: weather — humidity line, clock in spacer zone */
        lv_obj_remove_flag(lbl_feel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_hum_out, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_div_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_clock_alt, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_date_info, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_room_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_div_room, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_div3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_clock_gap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_flex_grow(s_clock_gap, 0);
        lv_obj_remove_flag(lbl_wind_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_wind_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_wind_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_wind_kmh, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_gust_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_gust_bft, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_div1, CONTENT_W);
        lv_obj_set_width(s_div2, CONTENT_W);
        lv_obj_set_width(s_div_room, CONTENT_W);
        lv_obj_set_width(s_div3, CONTENT_W);
        lv_obj_set_width(s_wind_row, CONTENT_W);
        lv_obj_set_width(lbl_date_info, CONTENT_W);
        lv_obj_set_width(s_graph, CONTENT_W);
        lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_font(lbl_hum_out, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_font(lbl_wind_main, &lv_font_montserrat_36, 0);
        lv_obj_set_style_pad_bottom(lbl_temp, 2, 0);
        lv_obj_set_style_pad_bottom(lbl_feel, 2, 0);
        lv_obj_set_style_margin_bottom(s_div1, 14, 0);
        lv_obj_set_style_margin_bottom(s_div2, 14, 0);
        lv_obj_set_style_margin_bottom(s_wind_row, 0, 0);
        lv_obj_set_style_pad_bottom(lbl_hum_out, 14, 0);
        lv_obj_set_style_pad_bottom(lbl_wind_bft, 0, 0);
        lv_obj_set_style_pad_bottom(lbl_press_val, 0, 0);
        lv_obj_set_style_pad_bottom(s_graph, 2, 0);
        lv_obj_set_style_pad_bottom(lbl_trend_caption, 0, 0);
        lv_obj_set_style_pad_bottom(lbl_room_temp, 10, 0);
    }
}

/* ── Clock timer (1 s, reads system time set by SNTP) ────────────── */

static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);

    char buf[8];
    if (ti.tm_year > (2020 - 1900)) {
        snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }
    lv_label_set_text(lbl_clock, buf);
    lv_label_set_text(lbl_clock_alt, buf);

    char date_buf[48];
    if (ti.tm_year > (2020 - 1900)) {
        strftime(date_buf, sizeof(date_buf), "%A\n%d %B", &ti);
    } else {
        snprintf(date_buf, sizeof(date_buf), "---");
    }
    lv_label_set_text(lbl_date_info, date_buf);
}

/* ── Public API ────────────────────────────────────────────────────── */

void ui_weather_create(lv_obj_t *parent)
{
    s_storm_alert = lv_obj_create(parent);
    lv_obj_remove_style_all(s_storm_alert);
    lv_obj_set_pos(s_storm_alert, ALERT_X, ALERT_Y);
    lv_obj_set_size(s_storm_alert, ALERT_SIZE, ALERT_SIZE);
    lv_obj_clear_flag(s_storm_alert, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_storm_alert, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_storm_alert, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_storm_alert, storm_alert_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    apply_storm_alert_style();

    /* ── Transparent column container (no card background) ─────────── */
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_pos(col, COL_X, COL_Y);
    lv_obj_set_size(col, COL_W, COL_H);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(col, 0, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(col, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(col, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* ── Hero temperature ──────────────────────────────────────────── */
    lbl_temp = make_label(col, &lv_font_montserrat_48, col_hero(),
                          "11.7\u00B0C");
    lv_obj_set_style_pad_bottom(lbl_temp, 2, 0);

    /* ── Feels-like (compact, integer, subdued) ────────────────────── */
    lbl_feel = make_label(col, &lv_font_montserrat_20, col_text_muted(),
                          "Feels 1\u00B0C");
    lv_obj_set_style_pad_bottom(lbl_feel, 14, 0);   /* adjusted per page */

    /* ── Outdoor humidity (pages 0-1 only, same style as Feels) ─── */
    lbl_hum_out = make_label(col, &lv_font_montserrat_20, col_text_muted(),
                             "43% RH");
    lv_obj_set_style_pad_bottom(lbl_hum_out, 14, 0);

    /* ── Divider (margin switches per page) ──────────────────────── */
    s_div1 = make_separator(col, 14);

    /* ── Clock (page 2 only — NTP time, subordinate to hero temp) ── */
    lbl_clock = make_label(col, &lv_font_montserrat_28, col_text_secondary(),
                           "--:--");
    lv_obj_set_style_pad_bottom(lbl_clock, 14, 0);

    s_div_clock = make_separator(col, 14);

    /* ── Wind section (3 lines: label, value, km/h) ────────────────── */
    lbl_wind_title = make_label(col, &lv_font_montserrat_16, col_text_secondary(), "Wind");

    lbl_wind_main = make_label(col, &lv_font_montserrat_36,
                              theme_bft_color(3.0f), "3");
    lv_obj_set_style_margin_top(lbl_wind_main, 2, 0);

    s_wind_row = lv_obj_create(col);
    lv_obj_remove_style_all(s_wind_row);
    lv_obj_set_size(s_wind_row, CONTENT_W, LV_SIZE_CONTENT);
    lv_obj_clear_flag(s_wind_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_wind_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_wind_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_wind_row, 8, 0);
    lv_obj_set_style_margin_top(s_wind_row, 2, 0);

    lbl_wind_bft = make_label(s_wind_row, &lv_font_montserrat_28, theme_bft_color(3.0f), "3");
    lbl_wind_sep = make_label(s_wind_row, &lv_font_montserrat_28, col_text_secondary(), "/");
    lbl_wind_gust = make_label(s_wind_row, &lv_font_montserrat_28, theme_bft_color(5.0f), "5");

    lbl_wind_kmh = make_label(col, &lv_font_montserrat_12,
                              col_text_muted(), "15 km/h");
    lv_obj_set_style_margin_top(lbl_wind_kmh, 1, 0);
    lv_obj_set_style_pad_bottom(lbl_wind_kmh, 10, 0);

    /* ── Gust section (2 lines: label, value) ────────────────────── */
    lbl_gust_title = make_label(col, &lv_font_montserrat_16, col_text_secondary(), "Vlagen");

    lbl_gust_bft = make_label(col, &lv_font_montserrat_36,
                              theme_bft_color(5.0f), "5");
    lv_obj_set_style_margin_top(lbl_gust_bft, 2, 0);
    lv_obj_set_style_pad_bottom(lbl_gust_bft, 14, 0);

    /* ── Divider after gust (margin switches per page) ───────────── */
    s_div2 = make_separator(col, 14);

    s_clock_gap = lv_obj_create(col);
    lv_obj_remove_style_all(s_clock_gap);
    lv_obj_set_size(s_clock_gap, 1, 1);
    lv_obj_clear_flag(s_clock_gap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_clock_gap, LV_OBJ_FLAG_HIDDEN);

    /* ── Clock (pages 0-1 — in the zone between gust and pressure) ── */
    lbl_clock_alt = make_label(col, &lv_font_montserrat_28, col_text_secondary(),
                               "--:--");
    lv_obj_set_style_pad_bottom(lbl_clock_alt, 10, 0);

    lbl_date_info = make_label(col, &lv_font_montserrat_16, col_text_secondary(),
                               "---");
    lv_obj_set_style_pad_bottom(lbl_date_info, 8, 0);
    lv_label_set_long_mode(lbl_date_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_date_info, CONTENT_W);

    s_div_room = make_separator(col, 14);

    lbl_room_temp = make_label(col, &lv_font_montserrat_20, col_text_muted(),
                               "--.-\u00B0C");
    lv_obj_set_style_text_color(lbl_room_temp, col_text_secondary(), 0);
    lv_obj_set_style_pad_bottom(lbl_room_temp, 10, 0);

    /* ── Divider before pressure block ───────────────────────────── */
    s_div3 = make_separator(col, 14);

    /* ── Pressure + barometric trend (one visual unit) ────────────── */
    lbl_press_val = make_label(col, &lv_font_montserrat_14,
                               col_text_secondary(), "995 hPa");
    lv_obj_set_style_pad_bottom(lbl_press_val, 0, 0);

    s_graph = lv_obj_create(col);
    lv_obj_remove_style_all(s_graph);
    lv_obj_set_size(s_graph, CONTENT_W, GRAPH_H);
    lv_obj_clear_flag(s_graph, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_graph, graph_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_set_style_pad_bottom(s_graph, 2, 0);

    /* ── Trend caption ────────────────────────────────────────────── */
    lbl_trend_caption = make_label(col, &lv_font_montserrat_12,
               col_text_muted(), "last 24h");

    /* ── Pre-fill pressure history with gentle simulated trend ─────── */
    for (int i = 0; i < BARO_HISTORY; i++) {
        s_baro[i] = 995.0f + 3.0f * sinf((float)i * 0.15f);
    }
    s_baro_count = BARO_HISTORY;
    s_baro_idx   = 0;

    /* ── Clock update timer (1 s) — shows "--:--" until NTP syncs ── */
    lv_timer_create(clock_timer_cb, 1000, NULL);

    /* ── Initial layout: pages 0-1 (clock hidden, spacer visible) ── */
    apply_page_layout();
}

void ui_weather_update(const demo_data_t *d)
{
    /* LVGL's lv_snprintf doesn't support %f -- use C snprintf */
    char buf[32];

    s_outdoor_temp = d->temp;
    s_feel_temp    = d->feel_temp;
    s_outdoor_hum  = d->humidity;
    refresh_hero();
    refresh_secondary();
    refresh_room_temp_line();

    snprintf(buf, sizeof(buf), "%.0f%% RH", (double)d->humidity);
    lv_label_set_text(lbl_hum_out, buf);

    if (s_page != 1) {
        snprintf(buf, sizeof(buf), "%.0f", (double)d->wind_bft);
        lv_label_set_text(lbl_wind_main, buf);
        lv_obj_set_style_text_color(lbl_wind_main, theme_bft_color(d->wind_bft), 0);
    } else {
        snprintf(buf, sizeof(buf), "%.0f", (double)d->wind_bft);
        lv_label_set_text(lbl_wind_bft, buf);
        lv_obj_set_style_text_color(lbl_wind_bft, theme_bft_color(d->wind_bft), 0);
        snprintf(buf, sizeof(buf), "%.0f", (double)d->gust_bft);
        lv_label_set_text(lbl_wind_gust, buf);
        lv_obj_set_style_text_color(lbl_wind_gust, theme_bft_color(d->gust_bft), 0);
    }

    snprintf(buf, sizeof(buf), "%d km/h", bft_to_kmh(d->wind_bft));
    lv_label_set_text(lbl_wind_kmh, buf);

    snprintf(buf, sizeof(buf), "%.0f", (double)d->gust_bft);
    lv_label_set_text(lbl_gust_bft, buf);
    lv_obj_set_style_text_color(lbl_gust_bft, theme_bft_color(d->gust_bft), 0);

    s_wind_bft = d->wind_bft;
    s_gust_bft = d->gust_bft;

    snprintf(buf, sizeof(buf), "%.0f hPa", (double)d->pressure);
    lv_label_set_text(lbl_press_val, buf);

    /* Push pressure into history buffer. panel_api overwrites the whole
     * buffer right after via ui_weather_set_baro_trend() using the API's
     * authoritative trend array, so this push is harmless. It is kept so
     * the trend still grows naturally if the weather source only supplies
     * a current value (e.g. demo_data mode). */
    s_baro[s_baro_idx] = d->pressure;
    s_baro_idx = (s_baro_idx + 1) % BARO_HISTORY;
    if (s_baro_count < BARO_HISTORY) s_baro_count++;

    lv_obj_invalidate(s_graph);
}

void ui_weather_set_baro_trend(const float *values, int count)
{
    if (!values || count <= 0) return;
    if (count > BARO_HISTORY) count = BARO_HISTORY;

    for (int i = 0; i < count; i++) s_baro[i] = values[i];
    s_baro_count = count;
    s_baro_idx   = 0;

    if (s_graph) lv_obj_invalidate(s_graph);
}

void ui_weather_set_room_climate(float temp_c, bool temp_valid,
                                  float rh_pct, bool rh_valid)
{
    s_room_temp    = temp_c;
    s_room_temp_ok = temp_valid;
    s_room_rh      = rh_pct;
    s_room_rh_ok   = rh_valid;
    refresh_room_temp_line();
    if (s_page == 2) {
        refresh_hero();
        refresh_secondary();
    }
}

void ui_weather_set_page(int page)
{
    if (page == s_page) return;
    s_page = page;
    apply_page_layout();
    refresh_hero();
    refresh_secondary();
}

void ui_weather_apply_theme(void)
{
    if (lbl_temp == NULL) {
        return;
    }

    lv_obj_set_style_text_color(lbl_temp, col_hero(), 0);
    lv_obj_set_style_text_color(lbl_feel, col_text_muted(), 0);
    lv_obj_set_style_text_color(lbl_hum_out, col_text_muted(), 0);
    lv_obj_set_style_text_color(lbl_clock, col_text_secondary(), 0);
    lv_obj_set_style_text_color(lbl_clock_alt, col_text_secondary(), 0);
    lv_obj_set_style_text_color(lbl_date_info, col_text_secondary(), 0);
    lv_obj_set_style_text_color(lbl_room_temp, col_text_secondary(), 0);
    lv_obj_set_style_text_color(lbl_press_val, col_text_secondary(), 0);
    lv_obj_set_style_text_color(lbl_trend_caption, col_text_muted(), 0);
    lv_obj_set_style_text_color(lbl_wind_title, col_text_secondary(), 0);
    lv_obj_set_style_text_color(lbl_wind_sep, col_text_secondary(), 0);
    lv_obj_set_style_text_color(lbl_wind_kmh, col_text_muted(), 0);
    lv_obj_set_style_text_color(lbl_gust_title, col_text_secondary(), 0);

    lv_obj_set_style_bg_color(s_div1, col_separator(), 0);
    lv_obj_set_style_bg_color(s_div_clock, col_separator(), 0);
    lv_obj_set_style_bg_color(s_div2, col_separator(), 0);
    lv_obj_set_style_bg_color(s_div_room, col_separator(), 0);
    lv_obj_set_style_bg_color(s_div3, col_separator(), 0);

    lv_obj_set_style_text_color(lbl_wind_main, theme_bft_color(s_wind_bft), 0);
    lv_obj_set_style_text_color(lbl_wind_bft, theme_bft_color(s_wind_bft), 0);
    lv_obj_set_style_text_color(lbl_wind_gust, theme_bft_color(s_gust_bft), 0);
    lv_obj_set_style_text_color(lbl_gust_bft, theme_bft_color(s_gust_bft), 0);
    apply_storm_alert_style();

    lv_obj_invalidate(s_graph);
}

void ui_weather_set_storm_active(bool active)
{
    if (s_storm_active == active) return;
    s_storm_active = active;
    apply_storm_alert_style();
}
