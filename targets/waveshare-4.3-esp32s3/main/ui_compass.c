/**
 * @file  ui_compass.c
 * @brief Wind compass -- native LVGL draw primitives, instrument quality.
 *
 * All graphics via LVGL 9.2 draw API (lv_draw_arc, lv_draw_line,
 * lv_draw_triangle, lv_draw_rect) inside a custom draw-event handler.
 * Direction labels are lv_label children rendered on top.
 *
 * Tick hierarchy (reduced density):
 *   24 ticks at every 15 degrees (was 36 at every 10 degrees).
 *   Three levels: cardinal (longest, boldest), inter-cardinal (medium),
 *   and minor 15-degree marks (short, visible but unobtrusive).
 *
 * Needle pseudo-3D technique (kite shape):
 *   The needle is a kite/diamond shape, widest at a "shoulder" point
 *   slightly forward of the centre, tapering to a sharp tip in front
 *   and a blunt tail behind.  Each half is drawn as layered triangles
 *   sharing the shoulder base edge:
 *     Front (3 layers, bottom-to-top):
 *       1. Edge  (dark amber, hw=6)  -- shadow flanks
 *       2. Body  (warm amber, hw=4)  -- main visible surface
 *       3. Ridge (bright gold, hw=2) -- raised centre highlight
 *     Back/tail (2 layers):
 *       1. Edge  (dark, hw=6)        -- shadow
 *       2. Body  (dark surface, hw=4)-- counterweight
 *   The kite shape creates the illusion of a needle with real mass:
 *   wider near the shoulder, tapering toward both tip and tail.
 *
 * Hub pseudo-3D technique:
 *   Four layered circles (bottom-to-top):
 *     1. Shadow halo  (R=11, very dark)   -- depth behind the cap
 *     2. Body + rim   (R=9, dark, 1px bright border) -- machined edge
 *     3. Inner face   (R=5, darker)       -- recessed centre
 *     4. Centre dot   (R=2, bright warm)  -- pivot highlight
 *
 * Compass centred at (220, 195) within the page-right container
 * (which sits at screen x=300), so screen position is (520, 195).
 */

#include "ui_compass.h"
#include "buc_display.h"
#include <math.h>
#include <stdio.h>
#include <stdbool.h>

/* ── Compass rose geometry ──────────────────────────────────────────── */
#define CX              150       /* centre X within container */
#define CY              150       /* centre Y within container */

#define R_OUTER         138       /* outer ring centre-radius */
#define RING_W            2       /* outer ring stroke width */
#define R_TICK_START    136       /* tick outer edge (flush with ring inner) */
#define R_LABEL         100       /* direction label placement radius */

/* Tick lengths (inward from R_TICK_START) -- reduced set, every 15 deg */
#define TICK_CARDINAL    20       /* N / O / Z / W */
#define TICK_INTERCARD   14       /* NO / ZO / ZW / NW */
#define TICK_MINOR        8       /* remaining 15-degree positions */

/* ── Needle geometry -- kite shape, layered pseudo-3D ───────────────── */
#define R_NEEDLE        115       /* front tip radius */
#define R_SHOULDER       28       /* widest point, forward of centre */
#define R_TAIL           35       /* tail tip radius (behind centre) */

/* Half-widths at the shoulder (shared by front and tail triangles) */
#define EDGE_HW           6       /* edge/shadow layer */
#define BODY_HW           4       /* main body layer */
#define RIDGE_HW          2       /* highlight ridge (front only) */

/* ── Hub geometry -- four-layer pseudo-3D ──────────────────────────── */
#define HUB_SHADOW_R     11       /* outer shadow halo */
#define HUB_BODY_R        9       /* main body (with rim highlight) */
#define HUB_INNER_R       5       /* recessed inner face */
#define HUB_DOT_R         2       /* centre bright dot */

/* ── Container position (relative to page-right parent at x=300) ───── */
#define COMP_CX         220       /* was 520, minus PAGE_RIGHT_X (300) */
#define COMP_CY         195
#define CONT_X          (COMP_CX - CX)
#define CONT_Y          (COMP_CY - CY)
#define CONT_W          (CX * 2)
#define CONT_H          (CY * 2)

/* ── Needle colours (pseudo-3D: dark edge -> body -> bright ridge) ─── */
/* Normal (connected / valid data) -- amber/gold */
#define COL_NEEDLE_EDGE_OK    lv_color_hex(0x805828)  /* dark amber flank */
#define COL_NEEDLE_BODY_OK    lv_color_hex(0xD09030)  /* warm amber surface */
#define COL_NEEDLE_RIDGE_OK   lv_color_hex(0xF0D060)  /* bright gold highlight */

/* Error (no WiFi / no data) -- red variant, same pseudo-3D layering */
#define COL_NEEDLE_EDGE_ERR   lv_color_hex(0x701818)  /* dark red flank */
#define COL_NEEDLE_BODY_ERR   lv_color_hex(0xC02828)  /* deep red surface */
#define COL_NEEDLE_RIDGE_ERR  lv_color_hex(0xF05050)  /* bright red highlight */

/* Tail (counterweight) -- two layers, same in both states */
#define COL_TAIL_EDGE     lv_color_hex(0x101820)  /* dark tail shadow */
#define COL_TAIL_BODY     lv_color_hex(0x1A2838)  /* dark tail surface */

/* ── Hub colours (pseudo-3D: shadow -> body+rim -> inner -> dot) ───── */
#define COL_HUB_SHADOW    lv_color_hex(0x141C28)  /* shadow halo */
#define COL_HUB_BODY      lv_color_hex(0x283848)  /* hub body */
#define COL_HUB_RIM       lv_color_hex(0x587080)  /* machined rim highlight */
#define COL_HUB_INNER     lv_color_hex(0x182430)  /* recessed face */
#define COL_HUB_DOT       lv_color_hex(0xD8E0E8)  /* pivot dot */

/* ── Runtime state ──────────────────────────────────────────────────── */
static float     s_dir  = 225.0f;
static lv_obj_t *s_cont;
static lv_obj_t *s_lbl_deg;
static bool      s_connected = false;   /* start red at boot until WiFi up */

/* ── Polar -> pixel (0 deg = North, clockwise) ─────────────────────── */
static inline void pol(float deg, float r, float *x, float *y)
{
    float a = deg * (float)M_PI / 180.0f;
    *x = (float)CX + r * sinf(a);
    *y = (float)CY - r * cosf(a);
}

/* ── Perpendicular offsets at an arbitrary point along the axis ─────── */
static inline void perp_at(float px, float py, float sn, float cs,
                           float hw,
                           float *lx, float *ly, float *rx, float *ry)
{
    *lx = px + hw * cs;
    *ly = py + hw * sn;
    *rx = px - hw * cs;
    *ry = py - hw * sn;
}

/* Draw a single filled triangle. */
static void draw_tri(lv_layer_t *layer, int32_t ox, int32_t oy,
                     float tip_x, float tip_y,
                     float bl_x, float bl_y,
                     float br_x, float br_y,
                     lv_color_t color)
{
    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.bg_color = color;
    t.bg_opa   = LV_OPA_COVER;
    t.p[0].x = (lv_value_precise_t)tip_x + ox;
    t.p[0].y = (lv_value_precise_t)tip_y + oy;
    t.p[1].x = (lv_value_precise_t)bl_x  + ox;
    t.p[1].y = (lv_value_precise_t)bl_y  + oy;
    t.p[2].x = (lv_value_precise_t)br_x  + ox;
    t.p[2].y = (lv_value_precise_t)br_y  + oy;
    lv_draw_triangle(layer, &t);
}

/* Draw a filled circle (used for hub layers). */
static void draw_disc(lv_layer_t *layer, int32_t cx, int32_t cy,
                      int32_t r, lv_color_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color;
    d.bg_opa   = opa;
    d.radius   = LV_RADIUS_CIRCLE;

    lv_area_t a;
    a.x1 = cx - r;  a.y1 = cy - r;
    a.x2 = cx + r;  a.y2 = cy + r;
    lv_draw_rect(layer, &d, &a);
}

/* ── Draw-event callback ───────────────────────────────────────────── */
/*
 * Draw order (back to front):
 *   ring -> ticks -> tail(edge,body) -> needle(edge,body,ridge) ->
 *   hub(shadow, body+rim, inner, dot)
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

    /* ── 1. Outer ring ─────────────────────────────────────────────── */
    {
        lv_draw_arc_dsc_t a;
        lv_draw_arc_dsc_init(&a);
        a.color       = COL_RING;
        a.width       = RING_W;
        a.center.x    = ox + CX;
        a.center.y    = oy + CY;
        a.radius      = R_OUTER;
        a.start_angle = 0;
        a.end_angle   = 360;
        lv_draw_arc(layer, &a);
    }

    /* ── 2. Tick marks -- every 15 deg (24 ticks, was 36 at 10 deg) ─ */
    for (int i = 0; i < 24; i++) {
        float deg = (float)(i * 15);
        int   ideg = i * 15;
        int   len, w;
        lv_color_t col;

        if      (ideg % 90 == 0) { len = TICK_CARDINAL;  w = 3; col = COL_TICK_MAJOR; }
        else if (ideg % 45 == 0) { len = TICK_INTERCARD;  w = 2; col = COL_TICK_MAJOR; }
        else                     { len = TICK_MINOR;       w = 2; col = COL_TICK_MINOR; }

        float x1, y1, x2, y2;
        pol(deg, (float)R_TICK_START, &x1, &y1);
        pol(deg, (float)(R_TICK_START - len), &x2, &y2);

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

    /* ═══════════════════════════════════════════════════════════════ */
    /* ── 3. NEEDLE -- kite shape, pseudo-3D via layered triangles ── */
    /* ═══════════════════════════════════════════════════════════════ */
    /*
     * The needle is a kite/diamond: two sets of triangles sharing a
     * common base at the shoulder point.  Front triangles taper from
     * shoulder to tip; back triangles taper from shoulder to tail.
     * Three colour layers on front, two on tail.
     */

    float rad = s_dir * (float)M_PI / 180.0f;
    float sn  = sinf(rad);
    float cs  = cosf(rad);

    /* Key points along the needle axis */
    float tip_x  = (float)CX + (float)R_NEEDLE   * sn;
    float tip_y  = (float)CY - (float)R_NEEDLE   * cs;
    float sh_x   = (float)CX + (float)R_SHOULDER * sn;
    float sh_y   = (float)CY - (float)R_SHOULDER * cs;
    float tail_x = (float)CX - (float)R_TAIL     * sn;
    float tail_y = (float)CY + (float)R_TAIL     * cs;

    /* Shoulder perpendicular points for each layer width */
    float el_x, el_y, er_x, er_y;   /* edge (widest) */
    float bl_x, bl_y, br_x, br_y;   /* body (medium) */
    float rl_x, rl_y, rr_x, rr_y;   /* ridge (narrowest, front only) */

    perp_at(sh_x, sh_y, sn, cs, (float)EDGE_HW,
            &el_x, &el_y, &er_x, &er_y);
    perp_at(sh_x, sh_y, sn, cs, (float)BODY_HW,
            &bl_x, &bl_y, &br_x, &br_y);
    perp_at(sh_x, sh_y, sn, cs, (float)RIDGE_HW,
            &rl_x, &rl_y, &rr_x, &rr_y);

    /* 3a. Tail -- edge layer (darkest, widest) */
    draw_tri(layer, ox, oy, tail_x, tail_y,
             el_x, el_y, er_x, er_y, COL_TAIL_EDGE);

    /* 3b. Tail -- body layer (dark surface, 2px edge strip visible) */
    draw_tri(layer, ox, oy, tail_x, tail_y,
             bl_x, bl_y, br_x, br_y, COL_TAIL_BODY);

    /* Needle colours depend on content/connectivity state. Pseudo-3D
     * layering stays identical in both states -- only the hue shifts. */
    const lv_color_t c_edge  = s_connected ? COL_NEEDLE_EDGE_OK  : COL_NEEDLE_EDGE_ERR;
    const lv_color_t c_body  = s_connected ? COL_NEEDLE_BODY_OK  : COL_NEEDLE_BODY_ERR;
    const lv_color_t c_ridge = s_connected ? COL_NEEDLE_RIDGE_OK : COL_NEEDLE_RIDGE_ERR;

    /* 3c. Front -- edge layer (widest) */
    draw_tri(layer, ox, oy, tip_x, tip_y,
             el_x, el_y, er_x, er_y, c_edge);

    /* 3d. Front -- body layer (2px dark edge each side) */
    draw_tri(layer, ox, oy, tip_x, tip_y,
             bl_x, bl_y, br_x, br_y, c_body);

    /* 3e. Front -- ridge highlight (narrow centre strip) */
    draw_tri(layer, ox, oy, tip_x, tip_y,
             rl_x, rl_y, rr_x, rr_y, c_ridge);

    /* ═══════════════════════════════════════════════════════════════ */
    /* ── 4. HUB -- pseudo-3D layered cap ──────────────────────────── */
    /* ═══════════════════════════════════════════════════════════════ */

    int32_t hcx = ox + CX;
    int32_t hcy = oy + CY;

    /* 4a. Shadow halo -- creates depth behind the cap */
    draw_disc(layer, hcx, hcy, HUB_SHADOW_R, COL_HUB_SHADOW, LV_OPA_COVER);

    /* 4b. Body with machined rim highlight */
    {
        lv_draw_rect_dsc_t r;
        lv_draw_rect_dsc_init(&r);
        r.bg_color     = COL_HUB_BODY;
        r.bg_opa       = LV_OPA_COVER;
        r.radius       = LV_RADIUS_CIRCLE;
        r.border_color = COL_HUB_RIM;
        r.border_width = 1;
        r.border_opa   = LV_OPA_COVER;

        lv_area_t hub;
        hub.x1 = hcx - HUB_BODY_R;
        hub.y1 = hcy - HUB_BODY_R;
        hub.x2 = hcx + HUB_BODY_R;
        hub.y2 = hcy + HUB_BODY_R;
        lv_draw_rect(layer, &r, &hub);
    }

    /* 4c. Recessed inner face */
    draw_disc(layer, hcx, hcy, HUB_INNER_R, COL_HUB_INNER, LV_OPA_COVER);

    /* 4d. Centre dot -- bright pivot highlight */
    draw_disc(layer, hcx, hcy, HUB_DOT_R, COL_HUB_DOT, LV_OPA_80);
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
        lv_label_set_text(lbl, dirs[i].text);
        lv_obj_set_style_text_font(lbl, dirs[i].font, 0);
        lv_obj_set_style_text_color(lbl,
            dirs[i].cardinal ? COL_CARDINAL : COL_LIGHT_GREY, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);

        /* Centre each label on its polar coordinate */
        lv_obj_update_layout(lbl);
        lv_obj_set_pos(lbl,
                        (int32_t)lx - lv_obj_get_width(lbl) / 2,
                        (int32_t)ly - lv_obj_get_height(lbl) / 2);
    }

    /* ── Degree readout (subtle, centred below compass) ────────────── */
    s_lbl_deg = lv_label_create(parent);
    lv_obj_set_style_text_font(s_lbl_deg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_deg, COL_MED_GREY, 0);
    lv_obj_set_style_text_align(s_lbl_deg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_deg, 50);
    lv_label_set_text(s_lbl_deg, "225\u00B0");
    lv_obj_set_pos(s_lbl_deg, COMP_CX - 25, CONT_Y + CONT_H + 6);

    ui_compass_set_direction(225.0f);
}

/* ── Public: update needle direction ───────────────────────────────── */

void ui_compass_set_direction(float deg)
{
    s_dir = deg;
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
