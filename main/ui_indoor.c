/**
 * @file  ui_indoor.c
 * @brief Indoor climate matrix — 3 floor rows × 4 zone cells.
 *
 * Layout:
 *   Row 0  "UPPER FLOOR"   : Office   Bathroom  Bedroom     Wardrobe
 *   Row 1  "MAIN FLOOR"    : Kitchen  Living    Library     Sunroom
 *   Row 2  "LOWER LEVEL"   : Servers  Laundry   Utility     Studio
 *
 * Each cell (top to bottom):
 *   - Room name    (Montserrat 16, 0x9CA4BC — clear context, first read)
 *   - Temperature  (Montserrat 28, 0xD0D8E8 — soft white, dominant value)
 *   - RH           (Montserrat 14, COL_LIGHT_GREY — secondary)
 *
 * Row labels ("BOVEN", etc.) are Montserrat 14, uppercase with letter
 * spacing — structural anchors that bind each row visually.
 *
 * A 2 px left-accent line per cell carries an indoor-optimised colour
 * (blue → teal → green → yellow-green → gold → orange → red, tuned to
 * the 15–27 °C comfort range) for at-a-glance scanning.  Missing-data
 * cells fall back to a dim neutral accent.
 */

#include "ui_indoor.h"
#include "buc_display.h"
#include <stdio.h>

/* ── Grid geometry ─────────────────────────────────────────────────── */
#define COLS            4
#define ROWS            3
#define ZONES           (ROWS * COLS)

/* Available parent area: 500 × 480 */
#define GRID_X           8       /* left margin inside parent            */
#define GRID_Y          12       /* top margin                           */

#define ROW_LABEL_H     18       /* floor label line height  (14 pt)     */
#define ROW_GAP           4       /* coupling: label → cells             */
#define CELL_W         115       /* zone cell width                      */
#define CELL_H         115       /* zone cell height                     */
#define COL_GAP           6       /* horizontal gap between cells        */
#define ROW_SPACING      22       /* vertical gap between row blocks     */

/* 2 px accent line: temperature indicator */
#define ACCENT_W          2

/* ── Colour hierarchy ─────────────────────────────────────────────── */
/* Scanning order: room name → temperature → RH.                       */
#define COL_ROOM_NAME      lv_color_hex(0x9CA4BC)   /* clear context    */
#define COL_TEMP           lv_color_hex(0xD0D8E8)   /* soft white       */
#define COL_RH             COL_LIGHT_GREY            /* 0xB0B8C8        */
#define COL_ROW_LABEL      lv_color_hex(0x606878)   /* structural guide */
#define COL_UNAVAILABLE    COL_MED_GREY              /* calm placeholder */
#define COL_ACCENT_NEUTRAL lv_color_hex(0x1C2030)   /* missing-data bar */

/* ── Cell internal positions ──────────────────────────────────────── */
#define CELL_CX        (ACCENT_W + 4)   /* content left after accent    */
#define NAME_Y          2                /* room name top   (16 pt)     */
#define TEMP_Y         24                /* temperature top (28 pt)     */
#define RH_Y           62                /* humidity top    (14 pt)     */

/* Accent spans the full content area: room name through RH bottom */
#define ACCENT_Y       NAME_Y
#define ACCENT_H       (RH_Y + 16 - NAME_Y)

/* ── Room / floor names ───────────────────────────────────────────── */
static const char *floor_names[ROWS] = {
    "UPPER FLOOR", "MAIN FLOOR", "LOWER LEVEL"
};

static const char *room_names[ZONES] = {
    "Office",    "Bathroom",    "Bedroom",    "Wardrobe",
    "Kitchen",   "Living",      "Library",    "Sunroom",
    "Servers",   "Laundry",     "Utility",    "Studio",
};

/* ── Runtime handles ──────────────────────────────────────────────── */
static lv_obj_t *s_accent[ZONES];
static lv_obj_t *s_lbl_temp[ZONES];
static lv_obj_t *s_lbl_rh[ZONES];

/* ── Temperature → accent colour ──────────────────────────────────── */
/*
 * Indoor-optimised 7-stop scale.  The 15–27 °C range is stretched so
 * rooms in the typical comfort zone (17–23 °C) get more colour contrast
 * and outliers are immediately obvious.
 *
 *   ≤15 °C  blue        0x3C78A8
 *    17 °C  teal        0x3E8C8A
 *    19 °C  green       0x62A05A
 *    21 °C  yellow-grn  0x8FA84A
 *    23 °C  gold        0xB08C3C
 *    25 °C  orange      0xB06C38
 *   ≥27 °C  red         0xA04840
 */
static lv_color_t temp_to_accent(float temp_c)
{
    static const float    stops_t[] = {15,17,19,21,23,25,27};
    static const uint32_t stops_c[] = {
        0x3C78A8, 0x3E8C8A, 0x62A05A, 0x8FA84A, 0xB08C3C, 0xB06C38, 0xA04840
    };
    enum { N = 7 };

    if (temp_c <= stops_t[0])     return lv_color_hex(stops_c[0]);
    if (temp_c >= stops_t[N - 1]) return lv_color_hex(stops_c[N - 1]);

    for (int i = 0; i < N - 1; i++) {
        if (temp_c <= stops_t[i + 1]) {
            float frac = (temp_c - stops_t[i])
                       / (stops_t[i + 1] - stops_t[i]);
            uint8_t mix = (uint8_t)((1.0f - frac) * 255.0f);
            return lv_color_mix(lv_color_hex(stops_c[i]),
                                lv_color_hex(stops_c[i + 1]), mix);
        }
    }
    return lv_color_hex(stops_c[N - 1]);   /* unreachable */
}

/* ── Helpers ───────────────────────────────────────────────────────── */

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_color_t color, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

static void make_cell(lv_obj_t *parent, int32_t x, int32_t y, int idx)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_size(cell, CELL_W, CELL_H);
    lv_obj_clear_flag(cell,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* 2 px left accent — temperature indicator */
    s_accent[idx] = lv_obj_create(cell);
    lv_obj_remove_style_all(s_accent[idx]);
    lv_obj_clear_flag(s_accent[idx],
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_accent[idx], 0, ACCENT_Y);
    lv_obj_set_size(s_accent[idx], ACCENT_W, ACCENT_H);
    lv_obj_set_style_bg_color(s_accent[idx], COL_ACCENT_NEUTRAL, 0);
    lv_obj_set_style_bg_opa(s_accent[idx], LV_OPA_COVER, 0);

    /* Room name — first thing the eye identifies */
    lv_obj_t *name = make_label(cell, &lv_font_montserrat_16,
                                COL_ROOM_NAME, room_names[idx]);
    lv_obj_set_pos(name, CELL_CX, NAME_Y);

    /* Temperature — dominant value, soft white */
    s_lbl_temp[idx] = make_label(cell, &lv_font_montserrat_28,
                                 COL_UNAVAILABLE, "--");
    lv_obj_set_pos(s_lbl_temp[idx], CELL_CX, TEMP_Y);

    /* Relative humidity — secondary */
    s_lbl_rh[idx] = make_label(cell, &lv_font_montserrat_14,
                               COL_UNAVAILABLE, "--%");
    lv_obj_set_pos(s_lbl_rh[idx], CELL_CX, RH_Y);
}

/* ── Public API ────────────────────────────────────────────────────── */

void ui_indoor_create(lv_obj_t *parent)
{
    int32_t y = GRID_Y;

    for (int row = 0; row < ROWS; row++) {
        /* Floor label — structural anchor, small-caps feel */
        lv_obj_t *flbl = make_label(parent, &lv_font_montserrat_14,
                                    COL_ROW_LABEL, floor_names[row]);
        lv_obj_set_style_text_letter_space(flbl, 3, 0);
        lv_obj_set_pos(flbl, GRID_X + CELL_CX, y);
        y += ROW_LABEL_H + ROW_GAP;

        for (int col = 0; col < COLS; col++) {
            int idx = row * COLS + col;
            int32_t cx = GRID_X + col * (CELL_W + COL_GAP);
            make_cell(parent, cx, y, idx);
        }
        y += CELL_H + ROW_SPACING;
    }
}

void ui_indoor_update(int index, float temp_c, float rh_pct,
                      bool temp_valid, bool rh_valid)
{
    if (index < 0 || index >= ZONES) return;

    char buf[16];

    /* Accent line: temperature colour when valid, neutral when missing */
    lv_obj_set_style_bg_color(s_accent[index],
        temp_valid ? temp_to_accent(temp_c) : COL_ACCENT_NEUTRAL, 0);

    /* Temperature */
    if (temp_valid) {
        snprintf(buf, sizeof(buf), "%.1f\u00B0", (double)temp_c);
        lv_label_set_text(s_lbl_temp[index], buf);
        lv_obj_set_style_text_color(s_lbl_temp[index], COL_TEMP, 0);
    } else {
        lv_label_set_text(s_lbl_temp[index], "--");
        lv_obj_set_style_text_color(s_lbl_temp[index], COL_UNAVAILABLE, 0);
    }

    /* Relative humidity */
    if (rh_valid) {
        snprintf(buf, sizeof(buf), "%.0f%%", (double)rh_pct);
        lv_label_set_text(s_lbl_rh[index], buf);
        lv_obj_set_style_text_color(s_lbl_rh[index], COL_RH, 0);
    } else {
        lv_label_set_text(s_lbl_rh[index], "--%");
        lv_obj_set_style_text_color(s_lbl_rh[index], COL_UNAVAILABLE, 0);
    }
}
