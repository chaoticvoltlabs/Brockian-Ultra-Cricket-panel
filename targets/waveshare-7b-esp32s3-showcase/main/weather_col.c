#include "weather_col.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

/* Layout on the 7" (1024×600) indoor page.
 * Scaled from the 4.3" reference (COL_X=30, COL_Y=28, COL_W=270) so visual
 * proportions carry over: left-aligned column taking ~27% of the screen. */
#define COL_X            30
#define COL_Y            30
#define COL_W           280
#define COL_H           540

#define CONTENT_W       ((COL_W * 55) / 100)

#define BARO_HISTORY     60
#define GRAPH_H          40
#define BARO_MIN_RANGE   6.0f

#define COL_HERO         lv_color_hex(0xE2D4B0)
#define COL_SECONDARY    lv_color_hex(0xC8BA94)
#define COL_MUTED        lv_color_hex(0x8B836F)
#define COL_SEPARATOR    lv_color_hex(0x2A241D)
#define COL_GRAPH_LINE   lv_color_hex(0xC8BA94)

static lv_obj_t *lbl_temp;
static lv_obj_t *lbl_feel;
static lv_obj_t *lbl_hum;
static lv_obj_t *lbl_wind_main;
static lv_obj_t *lbl_wind_kmh;
static lv_obj_t *lbl_gust_bft;
static lv_obj_t *lbl_clock;
static lv_obj_t *lbl_press_val;
static lv_obj_t *s_graph;

static float s_baro[BARO_HISTORY];
static int   s_baro_idx;
static int   s_baro_count;

/* Beaufort → approximate km/h (same formula as the 4.3" column). */
static int bft_to_kmh(int bft)
{
    if (bft <= 0) return 0;
    return (int)(3.01f * powf((float)bft, 1.5f) + 0.5f);
}

typedef struct { float bft; uint8_t r, g, b; } grad_stop_t;

static const grad_stop_t GRAD[] = {
    {  0.0f, 0x73, 0x95, 0xB5 },
    {  2.0f, 0x62, 0xA0, 0x8A },
    {  4.0f, 0x8F, 0xA8, 0x4A },
    {  6.0f, 0xC1, 0x96, 0x2E },
    {  8.0f, 0xC5, 0x74, 0x2A },
    { 10.0f, 0xB0, 0x54, 0x38 },
    { 12.0f, 0x8E, 0x3F, 0x48 },
};
#define GRAD_STOPS  ((int)(sizeof(GRAD) / sizeof(GRAD[0])))

static lv_color_t bft_color(float bft)
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

    return lv_color_make(
        (uint8_t)((int)GRAD[seg].r + (int)((int)GRAD[seg + 1].r - (int)GRAD[seg].r) * t),
        (uint8_t)((int)GRAD[seg].g + (int)((int)GRAD[seg + 1].g - (int)GRAD[seg].g) * t),
        (uint8_t)((int)GRAD[seg].b + (int)((int)GRAD[seg + 1].b - (int)GRAD[seg].b) * t)
    );
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_color_t color, const char *text,
                            int32_t pad_top, int32_t pad_bottom)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    if (pad_top)    lv_obj_set_style_pad_top(lbl, pad_top, 0);
    if (pad_bottom) lv_obj_set_style_pad_bottom(lbl, pad_bottom, 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

/* A thin horizontal rule with a configurable bottom gap (as inner padding,
 * since LVGL v8 lacks margin style properties — pad_bottom on an empty
 * object still reserves space inside its own box, which the flex column
 * then honours). */
static void make_separator(lv_obj_t *parent, int32_t pad_bottom)
{
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_remove_style_all(sep);
    lv_obj_add_flag(sep, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(sep, CONTENT_W, 1 + pad_bottom);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sep, COL_SEPARATOR, 0);
    /* Background fills the whole 1+pad rect; that would paint a thick bar.
     * Instead clip the visible line to the top 1 px by overlaying a child
     * rect of pad_bottom px with the page background. */
    if (pad_bottom > 0) {
        lv_obj_t *gap = lv_obj_create(sep);
        lv_obj_remove_style_all(gap);
        lv_obj_set_size(gap, CONTENT_W, pad_bottom);
        lv_obj_align(gap, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_style_bg_color(gap, lv_color_hex(0x090909), 0);
        lv_obj_set_style_bg_opa(gap, LV_OPA_COVER, 0);
    }
}

/* Pressure trend graph — auto-scaled, min 6 hPa span. LVGL v8 draw API. */
static void graph_draw_cb(lv_event_t *e)
{
    if (s_baro_count < 2) return;

    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    if (draw_ctx == NULL) return;

    lv_obj_t *obj = lv_event_get_target(e);
    lv_area_t ca;
    lv_obj_get_coords(obj, &ca);

    const int32_t gx = ca.x1;
    const int32_t gy = ca.y1;
    const int32_t gw = lv_area_get_width(&ca);
    const int32_t gh = lv_area_get_height(&ca);

    float pmin = 1100.0f, pmax = 900.0f;
    for (int i = 0; i < s_baro_count; i++) {
        if (s_baro[i] < pmin) pmin = s_baro[i];
        if (s_baro[i] > pmax) pmax = s_baro[i];
    }

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
    ln.color       = COL_GRAPH_LINE;
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

        lv_point_t p1 = { .x = gx + (lv_coord_t)x0, .y = gy + (lv_coord_t)y0 };
        lv_point_t p2 = { .x = gx + (lv_coord_t)x1, .y = gy + (lv_coord_t)y1 };
        lv_draw_line(draw_ctx, &ln, &p1, &p2);
    }
}

void weather_col_create(lv_obj_t *parent)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_pos(col, COL_X, COL_Y);
    lv_obj_set_size(col, COL_W, COL_H);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(col, 0, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(col, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    lbl_temp = make_label(col, &lv_font_montserrat_48, COL_HERO,
                          "--.-\u00B0C", 0, 2);

    lbl_feel = make_label(col, &lv_font_montserrat_20, COL_MUTED,
                          "Feels --\u00B0C", 0, 2);

    lbl_hum = make_label(col, &lv_font_montserrat_20, COL_MUTED,
                         "--% RH", 0, 14);

    make_separator(col, 14);

    make_label(col, &lv_font_montserrat_16, COL_SECONDARY, "Wind", 0, 0);
    lbl_wind_main = make_label(col, &lv_font_montserrat_36, bft_color(0.0f),
                               "--", 2, 0);
    lbl_wind_kmh = make_label(col, &lv_font_montserrat_14, COL_MUTED,
                              "-- km/h", 1, 10);

    make_label(col, &lv_font_montserrat_16, COL_SECONDARY, "Vlagen", 0, 0);
    lbl_gust_bft = make_label(col, &lv_font_montserrat_36, bft_color(0.0f),
                              "--", 2, 14);

    make_separator(col, 14);

    lbl_clock = make_label(col, &lv_font_montserrat_28, COL_SECONDARY,
                           "--:--", 0, 14);

    make_separator(col, 14);

    lbl_press_val = make_label(col, &lv_font_montserrat_16, COL_SECONDARY,
                               "-- hPa", 0, 2);

    s_graph = lv_obj_create(col);
    lv_obj_remove_style_all(s_graph);
    lv_obj_set_size(s_graph, CONTENT_W, GRAPH_H);
    lv_obj_clear_flag(s_graph, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_graph, graph_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_set_style_pad_bottom(s_graph, 2, 0);

    make_label(col, &lv_font_montserrat_14, COL_MUTED, "last 24h", 0, 0);

    s_baro_count = 0;
    s_baro_idx   = 0;
}

void weather_col_set_outdoor(float temp_c, bool temp_ok,
                             float feel_c, bool feel_ok,
                             int rh_pct, bool rh_ok)
{
    char buf[32];

    if (temp_ok)
        snprintf(buf, sizeof(buf), "%.1f\u00B0C", (double)temp_c);
    else
        snprintf(buf, sizeof(buf), "--.-\u00B0C");
    lv_label_set_text(lbl_temp, buf);

    if (feel_ok)
        snprintf(buf, sizeof(buf), "Feels %.0f\u00B0C", (double)feel_c);
    else
        snprintf(buf, sizeof(buf), "Feels --\u00B0C");
    lv_label_set_text(lbl_feel, buf);

    if (rh_ok)
        snprintf(buf, sizeof(buf), "%d%% RH", rh_pct);
    else
        snprintf(buf, sizeof(buf), "--%% RH");
    lv_label_set_text(lbl_hum, buf);
}

void weather_col_set_wind(int wind_bft, int gust_bft, int wind_kmh)
{
    char buf[16];

    snprintf(buf, sizeof(buf), "%d", wind_bft);
    lv_label_set_text(lbl_wind_main, buf);
    lv_obj_set_style_text_color(lbl_wind_main, bft_color((float)wind_bft), 0);

    if (wind_kmh <= 0) wind_kmh = bft_to_kmh(wind_bft);
    snprintf(buf, sizeof(buf), "%d km/h", wind_kmh);
    lv_label_set_text(lbl_wind_kmh, buf);

    snprintf(buf, sizeof(buf), "%d", gust_bft);
    lv_label_set_text(lbl_gust_bft, buf);
    lv_obj_set_style_text_color(lbl_gust_bft, bft_color((float)gust_bft), 0);
}

void weather_col_set_pressure(int hpa, bool hpa_ok,
                              const float *trend, int n)
{
    char buf[16];
    if (hpa_ok)
        snprintf(buf, sizeof(buf), "%d hPa", hpa);
    else
        snprintf(buf, sizeof(buf), "-- hPa");
    lv_label_set_text(lbl_press_val, buf);

    if (trend != NULL && n > 0) {
        if (n > BARO_HISTORY) {
            trend += (n - BARO_HISTORY);
            n = BARO_HISTORY;
        }
        for (int i = 0; i < n; i++) s_baro[i] = trend[i];
        s_baro_count = n;
        s_baro_idx   = 0;
        if (s_graph) lv_obj_invalidate(s_graph);
    }
}

void weather_col_tick(void)
{
    if (lbl_clock == NULL) return;

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
}
