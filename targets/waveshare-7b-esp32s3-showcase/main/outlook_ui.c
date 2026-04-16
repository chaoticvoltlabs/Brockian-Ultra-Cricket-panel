#include "outlook_ui.h"

#include <math.h>
#include <stdio.h>

#define OUT_BG          lv_color_hex(0x090909)
#define OUT_TEXT        lv_color_hex(0xC8BA94)
#define OUT_SUBTLE      lv_color_hex(0x8B836F)
#define OUT_ACCENT      lv_color_hex(0xC1962E)
#define OUT_ALERT       lv_color_hex(0xD8A23E)
#define OUT_SOFT        lv_color_hex(0x6E6658)
#define OUT_GRID        lv_color_hex(0x2A2825)
#define OUT_FILL        lv_color_hex(0x151515)

#define COMP_R          236
#define COMP_CX         240
#define COMP_CY         240
#define BEZEL_W         16

#define CLR_RING        lv_color_hex(0x1B1B1B)
#define CLR_FACE        lv_color_hex(0x121212)
#define CLR_HAND        lv_color_hex(0xC1962E)

static int16_t s_wind_points[8] = { 3, 4, 4, 5, 6, 5, 4, 3 };
static int16_t s_gust_points[8] = { 5, 6, 6, 7, 8, 7, 6, 5 };
static int16_t s_current_wind_bft = 3;
static int16_t s_current_gust_bft = 6;
static int16_t s_current_dir_deg  = 225;

static lv_obj_t *s_compass;
static lv_obj_t *s_wind_val_lbl;
static lv_obj_t *s_gust_val_lbl;
static lv_obj_t *s_wind_fill;
static lv_obj_t *s_gust_fill;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_wind_series;
static lv_chart_series_t *s_gust_series;
static lv_obj_t *s_peak_lbl;
static lv_obj_t *s_chart_ref_label;
static lv_obj_t *s_chart_ref_line;

static const char *nl_cardinal(int deg)
{
    deg = ((deg % 360) + 360) % 360;
    static const char *cards[] = { "N", "NO", "O", "ZO", "Z", "ZW", "W", "NW" };
    return cards[((deg + 22) / 45) % 8];
}

static void style_label(lv_obj_t *obj, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
}

static lv_color_t severity_color(int bft)
{
    if (bft <= 2) return lv_color_hex(0x5C8F6A);
    if (bft <= 4) return lv_color_hex(0x8D9A54);
    if (bft <= 6) return lv_color_hex(0xC1962E);
    if (bft <= 8) return lv_color_hex(0xD87E36);
    return lv_color_hex(0xC45142);
}

static void pol(float deg, float r, float *x, float *y)
{
    float a = deg * (float)M_PI / 180.0f;
    *x = (float)COMP_CX + r * sinf(a);
    *y = (float)COMP_CY - r * cosf(a);
}


static void outlook_compass_draw_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int32_t ox = a.x1;
    int32_t oy = a.y1;

    /* Bezel: two filled circles, matching clock_ui.c pattern */
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color     = CLR_RING;
    rdsc.bg_opa       = LV_OPA_COVER;
    rdsc.radius       = LV_RADIUS_CIRCLE;
    rdsc.border_width = 0;
    rdsc.shadow_width = 0;
    lv_area_t oa = {
        .x1 = (lv_coord_t)(ox + COMP_CX - COMP_R),
        .y1 = (lv_coord_t)(oy + COMP_CY - COMP_R),
        .x2 = (lv_coord_t)(ox + COMP_CX + COMP_R),
        .y2 = (lv_coord_t)(oy + COMP_CY + COMP_R),
    };
    lv_draw_rect(draw_ctx, &rdsc, &oa);

    rdsc.bg_color = CLR_FACE;
    lv_area_t fa = {
        .x1 = oa.x1 + BEZEL_W,
        .y1 = oa.y1 + BEZEL_W,
        .x2 = oa.x2 - BEZEL_W,
        .y2 = oa.y2 - BEZEL_W,
    };
    lv_draw_rect(draw_ctx, &rdsc, &fa);

    for (int i = 0; i < 24; ++i) {
        float deg = (float)(i * 15);
        int len = (i % 6 == 0) ? 20 : ((i % 3 == 0) ? 14 : 8);
        int width = (i % 6 == 0) ? 3 : 2;
        lv_color_t col = (i % 6 == 0) ? OUT_TEXT : OUT_SOFT;

        float x1, y1, x2, y2;
        pol(deg, (float)(COMP_R - 4), &x1, &y1);
        pol(deg, (float)(COMP_R - 4 - len), &x2, &y2);

        lv_draw_line_dsc_t ln;
        lv_draw_line_dsc_init(&ln);
        ln.color = col;
        ln.width = width;
        ln.round_start = 1;
        ln.round_end = 1;
        lv_point_t p1 = { (lv_coord_t)(ox + x1), (lv_coord_t)(oy + y1) };
        lv_point_t p2 = { (lv_coord_t)(ox + x2), (lv_coord_t)(oy + y2) };
        lv_draw_line(draw_ctx, &ln, &p1, &p2);
    }

    static const char *dirs[8] = { "N", "NO", "O", "ZO", "Z", "ZW", "W", "NW" };
    for (int i = 0; i < 8; ++i) {
        float deg = (float)(i * 45);
        float x, y;
        pol(deg, 185.0f, &x, &y);   /* just inside major-tick inner edge (212) */

        lv_draw_label_dsc_t td;
        lv_draw_label_dsc_init(&td);
        td.font = &lv_font_montserrat_20;
        td.color = OUT_TEXT;
        td.align = LV_TEXT_ALIGN_CENTER;

        lv_area_t la = {
            .x1 = (lv_coord_t)(ox + x - 26),
            .y1 = (lv_coord_t)(oy + y - 14),
            .x2 = (lv_coord_t)(ox + x + 26),
            .y2 = (lv_coord_t)(oy + y + 14),
        };
        lv_draw_label(draw_ctx, &td, &la, dirs[i], NULL);
    }

    float rad = s_current_dir_deg * (float)M_PI / 180.0f;
    float sn = sinf(rad);
    float cs = cosf(rad);
    /* Needle: thin line from tail to tip, same style as clock hands */
    float tip_x  = (float)(ox + COMP_CX) + 175.0f * sn;
    float tip_y  = (float)(oy + COMP_CY) - 175.0f * cs;
    float tail_x = (float)(ox + COMP_CX) - 38.0f * sn;
    float tail_y = (float)(oy + COMP_CY) + 38.0f * cs;

    lv_draw_line_dsc_t ndsc;
    lv_draw_line_dsc_init(&ndsc);
    ndsc.color       = CLR_HAND;
    ndsc.width       = 4;
    ndsc.round_start = 1;
    ndsc.round_end   = 1;
    lv_point_t np1 = { (lv_coord_t)tail_x, (lv_coord_t)tail_y };
    lv_point_t np2 = { (lv_coord_t)tip_x,  (lv_coord_t)tip_y  };
    lv_draw_line(draw_ctx, &ndsc, &np1, &np2);

    lv_draw_rect_dsc_t hub;
    lv_draw_rect_dsc_init(&hub);
    hub.bg_color = OUT_ACCENT;
    hub.bg_opa = LV_OPA_COVER;
    hub.radius = LV_RADIUS_CIRCLE;
    lv_area_t hub_area = {
        .x1 = ox + COMP_CX - 7,
        .y1 = oy + COMP_CY - 7,
        .x2 = ox + COMP_CX + 7,
        .y2 = oy + COMP_CY + 7,
    };
    lv_draw_rect(draw_ctx, &hub, &hub_area);

    /* Direction text inside compass, ~35 px above the Z label.
     * Z label centre: (COMP_CX, COMP_CY+185) = (240,425).
     * Text centre:    (COMP_CX, COMP_CY+150) = (240,390).          */
    char dir_text[24];
    snprintf(dir_text, sizeof(dir_text), "%s %d", nl_cardinal(s_current_dir_deg), s_current_dir_deg);
    lv_draw_label_dsc_t dir_dsc;
    lv_draw_label_dsc_init(&dir_dsc);
    dir_dsc.font  = &lv_font_montserrat_20;
    dir_dsc.color = OUT_TEXT;
    dir_dsc.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t dir_la = {
        .x1 = (lv_coord_t)(ox + COMP_CX - 60),
        .y1 = (lv_coord_t)(oy + COMP_CY + 150 - 13),
        .x2 = (lv_coord_t)(ox + COMP_CX + 60),
        .y2 = (lv_coord_t)(oy + COMP_CY + 150 + 13),
    };
    lv_draw_label(draw_ctx, &dir_dsc, &dir_la, dir_text, NULL);
}

void outlook_ui_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, OUT_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    /* ── Right column: compass — nudged right for more breathing room
     *  against the left-column copy and slider.
     *  Widget: x=448..928, y=46..526.
     *  Widget size = COMP_CX*2 × COMP_CY*2 = 480×480.               ───── */
    lv_obj_t *compass = lv_obj_create(parent);
    s_compass = compass;
    lv_obj_set_size(compass, 480, 480);
    lv_obj_set_pos(compass, 448, 46);
    lv_obj_set_style_bg_opa(compass, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(compass, 0, 0);
    lv_obj_set_style_pad_all(compass, 0, 0);
    lv_obj_set_style_shadow_width(compass, 0, 0);
    lv_obj_clear_flag(compass, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(compass, outlook_compass_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* ── Left column: header ────────────────────────────────────────── */
    /* Compass starts at x=424, so left column stays within x=40..420
     * (col_w=380), leaving a 4 px gap before the compass widget edge.  */

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Storm forecast");
    style_label(title, &lv_font_montserrat_28, OUT_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 40, 44);

    lv_obj_t *subtitle = lv_label_create(parent);
    lv_label_set_text(subtitle, "Now and next 48 hours");
    style_label(subtitle, &lv_font_montserrat_18, OUT_SUBTLE);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    /* ── Left column: current conditions ───────────────────────────── */
    lv_obj_t *now_label = lv_label_create(parent);
    lv_label_set_text(now_label, "Now");
    style_label(now_label, &lv_font_montserrat_18, OUT_SUBTLE);
    lv_obj_align(now_label, LV_ALIGN_TOP_LEFT, 40, 160);

    char wind_buf[8];
    snprintf(wind_buf, sizeof(wind_buf), "%d", s_current_wind_bft);
    lv_obj_t *wind_val = lv_label_create(parent);
    s_wind_val_lbl = wind_val;
    lv_label_set_text(wind_val, wind_buf);
    style_label(wind_val, &lv_font_montserrat_32, severity_color(s_current_wind_bft));
    lv_obj_align_to(wind_val, now_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    lv_obj_t *slash = lv_label_create(parent);
    lv_label_set_text(slash, "/");
    style_label(slash, &lv_font_montserrat_20, OUT_SUBTLE);
    lv_obj_align_to(slash, wind_val, LV_ALIGN_OUT_RIGHT_MID, 12, 2);

    char gust_buf[8];
    snprintf(gust_buf, sizeof(gust_buf), "%d", s_current_gust_bft);
    lv_obj_t *gust_val = lv_label_create(parent);
    s_gust_val_lbl = gust_val;
    lv_label_set_text(gust_val, gust_buf);
    style_label(gust_val, &lv_font_montserrat_32, severity_color(s_current_gust_bft));
    lv_obj_align_to(gust_val, slash, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

    lv_obj_t *unit = lv_label_create(parent);
    lv_label_set_text(unit, "bft");
    style_label(unit, &lv_font_montserrat_18, OUT_SUBTLE);
    lv_obj_align_to(unit, gust_val, LV_ALIGN_OUT_RIGHT_MID, 12, 2);

    /* Peak gust: own line directly below the values row */
    lv_obj_t *peak = lv_label_create(parent);
    s_peak_lbl = peak;
    lv_label_set_text(peak, "Peak gust 8 around +24h");
    style_label(peak, &lv_font_montserrat_18, OUT_SUBTLE);
    lv_obj_align_to(peak, wind_val, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    /* Wind/gust severity strip (col_w wide, thin) */
    lv_obj_t *strip = lv_obj_create(parent);
    lv_obj_set_size(strip, 380, 8);
    lv_obj_align_to(strip, peak, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 32);
    lv_obj_set_style_radius(strip, 4, 0);
    lv_obj_set_style_border_width(strip, 0, 0);
    lv_obj_set_style_bg_color(strip, lv_color_hex(0x171717), 0);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(strip, 0, 0);
    lv_obj_set_style_shadow_width(strip, 0, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    const int col_w   = 380;
    const int wind_w  = (col_w * s_current_wind_bft) / 12;
    const int gust_w  = (col_w * s_current_gust_bft) / 12;

    lv_obj_t *wind_fill = lv_obj_create(strip);
    s_wind_fill = wind_fill;
    lv_obj_set_size(wind_fill, wind_w, 8);
    lv_obj_set_pos(wind_fill, 0, 0);
    lv_obj_set_style_radius(wind_fill, 4, 0);
    lv_obj_set_style_border_width(wind_fill, 0, 0);
    lv_obj_set_style_bg_color(wind_fill, severity_color(s_current_wind_bft), 0);
    lv_obj_set_style_shadow_width(wind_fill, 0, 0);
    lv_obj_clear_flag(wind_fill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *gust_fill = lv_obj_create(strip);
    s_gust_fill = gust_fill;
    lv_obj_set_size(gust_fill, gust_w - wind_w, 8);
    lv_obj_set_pos(gust_fill, wind_w, 0);
    lv_obj_set_style_radius(gust_fill, 0, 0);
    lv_obj_set_style_border_width(gust_fill, 0, 0);
    lv_obj_set_style_bg_color(gust_fill, severity_color(s_current_gust_bft), 0);
    lv_obj_set_style_bg_opa(gust_fill, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(gust_fill, 0, 0);
    lv_obj_clear_flag(gust_fill, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Bottom: 48h tendency chart, left column width ─────────────── */
    /* chart: x=40..420, y=496..556  (BOTTOM_LEFT y_ofs=-44)           */
    /* markers:          y=556..576  (BOTTOM_LEFT y_ofs=-24)           */

    lv_obj_t *chart = lv_chart_create(parent);
    s_chart = chart;
    lv_obj_set_size(chart, col_w, 78);
    lv_obj_align(chart, LV_ALIGN_BOTTOM_LEFT, 40, -44);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(chart, 8);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 12);
    lv_chart_set_div_line_count(chart, 2, 4);
    lv_obj_set_style_line_color(chart, OUT_GRID, LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_30, LV_PART_MAIN);

    s_wind_series = lv_chart_add_series(chart, OUT_SOFT, LV_CHART_AXIS_PRIMARY_Y);
    s_gust_series = lv_chart_add_series(chart, OUT_ALERT, LV_CHART_AXIS_PRIMARY_Y);
    for (uint32_t i = 0; i < 8; ++i) {
        s_wind_series->y_points[i] = s_wind_points[i];
        s_gust_series->y_points[i] = s_gust_points[i];
    }
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(chart, 2, LV_PART_ITEMS);
    lv_chart_refresh(chart);

    int16_t max_bft = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        if (s_wind_points[i] > max_bft) max_bft = s_wind_points[i];
        if (s_gust_points[i] > max_bft) max_bft = s_gust_points[i];
    }

    const int chart_x = 40;
    const int chart_y = 478;
    const int chart_h = 78;
    const int chart_max = 12;
    const int ref_y = chart_y + chart_h - ((max_bft * chart_h) / chart_max);

    char ref_buf[16];
    snprintf(ref_buf, sizeof(ref_buf), "%d bft", max_bft);

    lv_obj_t *ref_label = lv_label_create(parent);
    s_chart_ref_label = ref_label;
    lv_label_set_text(ref_label, ref_buf);
    style_label(ref_label, &lv_font_montserrat_14, OUT_TEXT);
    lv_obj_align(ref_label, LV_ALIGN_TOP_LEFT, chart_x, ref_y - 11);

    lv_obj_t *ref_line = lv_obj_create(parent);
    s_chart_ref_line = ref_line;
    lv_obj_set_size(ref_line, 314, 2);
    lv_obj_set_pos(ref_line, chart_x + 66, ref_y);
    lv_obj_set_style_bg_color(ref_line, OUT_SUBTLE, 0);
    lv_obj_set_style_bg_opa(ref_line, LV_OPA_80, 0);
    lv_obj_set_style_radius(ref_line, 1, 0);
    lv_obj_set_style_border_width(ref_line, 0, 0);
    lv_obj_set_style_pad_all(ref_line, 0, 0);
    lv_obj_set_style_shadow_width(ref_line, 0, 0);
    lv_obj_clear_flag(ref_line, LV_OBJ_FLAG_SCROLLABLE);

    /* Time-axis markers beneath chart.
     * chart.x1=40, width=380, 8 points → point spacing = 47.5 px
     * point[i].x = 40 + 47.5*(i+0.5)
     * label.x1 = point.x − 21 = 42.75 + 47.5*i ≈ 43 + 48*i           */
    static const char *markers[] = { "now", "+6", "+12", "+18", "+24", "+30", "+36", "+48" };
    for (int i = 0; i < 8; ++i) {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, markers[i]);
        style_label(lbl, &lv_font_montserrat_16, OUT_SUBTLE);
        lv_obj_set_width(lbl, 42);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 43 + (i * 48), -24);
    }
}

void outlook_ui_set_current(int wind_bft, int gust_bft, int dir_deg)
{
    if (wind_bft < 0)  wind_bft = 0;
    if (wind_bft > 12) wind_bft = 12;
    if (gust_bft < 0)  gust_bft = 0;
    if (gust_bft > 12) gust_bft = 12;
    if (gust_bft < wind_bft) gust_bft = wind_bft;
    dir_deg = ((dir_deg % 360) + 360) % 360;

    s_current_wind_bft = (int16_t)wind_bft;
    s_current_gust_bft = (int16_t)gust_bft;
    s_current_dir_deg  = (int16_t)dir_deg;

    char buf[8];

    if (s_wind_val_lbl != NULL) {
        snprintf(buf, sizeof(buf), "%d", wind_bft);
        lv_label_set_text(s_wind_val_lbl, buf);
        lv_obj_set_style_text_color(s_wind_val_lbl, severity_color(wind_bft), 0);
    }

    if (s_gust_val_lbl != NULL) {
        snprintf(buf, sizeof(buf), "%d", gust_bft);
        lv_label_set_text(s_gust_val_lbl, buf);
        lv_obj_set_style_text_color(s_gust_val_lbl, severity_color(gust_bft), 0);
    }

    if (s_wind_fill != NULL && s_gust_fill != NULL) {
        const int col_w = 380;
        int wind_w = (col_w * wind_bft) / 12;
        int gust_w = (col_w * gust_bft) / 12;
        if (wind_w > col_w) wind_w = col_w;
        if (gust_w > col_w) gust_w = col_w;

        lv_obj_set_size(s_wind_fill, wind_w, 8);
        lv_obj_set_style_bg_color(s_wind_fill, severity_color(wind_bft), 0);

        lv_obj_set_size(s_gust_fill, gust_w - wind_w, 8);
        lv_obj_set_pos(s_gust_fill, wind_w, 0);
        lv_obj_set_style_bg_color(s_gust_fill, severity_color(gust_bft), 0);
    }

    if (s_compass != NULL) {
        lv_obj_invalidate(s_compass);
    }
}

void outlook_ui_set_forecast(const int wind_bft[8], const int gust_bft[8],
                             int peak_gust_bft, int peak_hour_offset)
{
    if (wind_bft == NULL || gust_bft == NULL) return;

    int16_t max_bft = 0;
    for (int i = 0; i < 8; ++i) {
        int w = wind_bft[i]; if (w < 0) w = 0; if (w > 12) w = 12;
        int g = gust_bft[i]; if (g < 0) g = 0; if (g > 12) g = 12;
        s_wind_points[i] = (int16_t)w;
        s_gust_points[i] = (int16_t)g;
        if (s_wind_points[i] > max_bft) max_bft = s_wind_points[i];
        if (s_gust_points[i] > max_bft) max_bft = s_gust_points[i];
    }

    if (s_chart != NULL && s_wind_series != NULL && s_gust_series != NULL) {
        for (uint32_t i = 0; i < 8; ++i) {
            s_wind_series->y_points[i] = s_wind_points[i];
            s_gust_series->y_points[i] = s_gust_points[i];
        }
        lv_chart_refresh(s_chart);
    }

    if (s_peak_lbl != NULL) {
        if (peak_gust_bft < 0)  peak_gust_bft = 0;
        if (peak_gust_bft > 12) peak_gust_bft = 12;
        if (peak_hour_offset < 0) peak_hour_offset = 0;
        lv_label_set_text_fmt(s_peak_lbl, "Peak gust %d around +%dh",
                              peak_gust_bft, peak_hour_offset);
    }

    if (s_chart_ref_label != NULL && s_chart_ref_line != NULL) {
        const int chart_x   = 40;
        const int chart_y   = 478;
        const int chart_h   = 78;
        const int chart_max = 12;
        const int ref_y     = chart_y + chart_h - ((max_bft * chart_h) / chart_max);
        lv_label_set_text_fmt(s_chart_ref_label, "%d bft", max_bft);
        lv_obj_align(s_chart_ref_label, LV_ALIGN_TOP_LEFT, chart_x, ref_y - 11);
        lv_obj_set_pos(s_chart_ref_line, chart_x + 66, ref_y);
    }
}
