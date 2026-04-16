#include "indoor_ui.h"

#include <stdio.h>

/* ── Grid geometry, scaled for 1024×600 tile with left weather column ──
 * Keep these numbers in sync as a set. Widening the cells or moving GRID_X
 * back left causes the 4x3 indoor grid to overlap the fixed weather column. */
#define COLS            4
#define ROWS            3

#define GRID_X         340   /* leaves ~300 px for the weather column */
#define GRID_Y         102

#define TITLE_X        GRID_X
#define TITLE_Y         42
#define SUBTITLE_Y      78

#define ROW_LABEL_H     18
#define ROW_GAP          4
#define CELL_W         160
#define CELL_H         118
#define COL_GAP         10
#define ROW_SPACING     14

#define ACCENT_W         4
#define CELL_PAD_X      16
#define CELL_PAD_TOP     8

/* ── Colour hierarchy ──────────────────────────────────────────────── */
#define COL_PAGE_BG       lv_color_hex(0x090909)
#define COL_TITLE         lv_color_hex(0xC8BA94)
#define COL_SUBTITLE      lv_color_hex(0x8B836F)
#define COL_ROOM_NAME     lv_color_hex(0xB7A479)
#define COL_TEMP          lv_color_hex(0xE2D4B0)
#define COL_RH            lv_color_hex(0xB0B8C8)
#define COL_ROW_LABEL     lv_color_hex(0x6E6658)
#define COL_UNAVAILABLE   lv_color_hex(0x707888)
#define COL_ACCENT_NEUTRAL lv_color_hex(0x3A3329)

/* ── Cell internal positions (relative to cell origin) ─────────────── */
#define CELL_CX        (ACCENT_W + CELL_PAD_X)
#define NAME_Y          CELL_PAD_TOP
#define TEMP_Y          30
#define RH_Y            84

#define ACCENT_Y       NAME_Y
#define ACCENT_H       (RH_Y + 18 - NAME_Y)

/* ── Room / floor names (1:1 with 4.3" ui_indoor.c) ────────────────── */
static const char *floor_names[ROWS] = {
    "BOVEN", "BEGANE GROND", "KELDER"
};

static const char *room_names[INDOOR_ZONES] = {
    "Kantoor",   "Badkamer",    "Slaapkamer", "Kleding",
    "Keuken",    "Woonkamer",   "Bibliotheek","Tuinkamer",
    "Servers",   "Laundry",     "Utility",    "Studio",
};

/* ── Runtime handles ──────────────────────────────────────────────── */
static lv_obj_t *s_accent[INDOOR_ZONES];
static lv_obj_t *s_lbl_temp[INDOOR_ZONES];
static lv_obj_t *s_lbl_rh[INDOOR_ZONES];
static lv_obj_t *s_lbl_name[INDOOR_ZONES];

/* ── Temperature → accent colour (7-stop, comfort-zone biased) ────── */
static lv_color_t temp_to_accent(float temp_c)
{
    static const float    stops_t[] = { 15, 17, 19, 21, 23, 25, 27 };
    static const uint32_t stops_c[] = {
        0x3C78A8, 0x3E8C8A, 0x62A05A, 0x8FA84A, 0xB08C3C, 0xB06C38, 0xA04840
    };
    enum { N = 7 };

    if (temp_c <= stops_t[0])     return lv_color_hex(stops_c[0]);
    if (temp_c >= stops_t[N - 1]) return lv_color_hex(stops_c[N - 1]);

    for (int i = 0; i < N - 1; i++) {
        if (temp_c <= stops_t[i + 1]) {
            float frac = (temp_c - stops_t[i]) / (stops_t[i + 1] - stops_t[i]);
            uint8_t mix = (uint8_t)((1.0f - frac) * 255.0f);
            return lv_color_mix(lv_color_hex(stops_c[i]),
                                lv_color_hex(stops_c[i + 1]), mix);
        }
    }
    return lv_color_hex(stops_c[N - 1]);
}

/* ── Helpers ──────────────────────────────────────────────────────── */
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_color_t color, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

static void make_cell(lv_obj_t *parent, int32_t x, int32_t y, int idx)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_size(cell, CELL_W, CELL_H);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_accent[idx] = lv_obj_create(cell);
    lv_obj_remove_style_all(s_accent[idx]);
    lv_obj_clear_flag(s_accent[idx], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_accent[idx], 0, ACCENT_Y);
    lv_obj_set_size(s_accent[idx], ACCENT_W, ACCENT_H);
    lv_obj_set_style_bg_color(s_accent[idx], COL_ACCENT_NEUTRAL, 0);
    lv_obj_set_style_bg_opa(s_accent[idx], LV_OPA_COVER, 0);

    s_lbl_name[idx] = make_label(cell, &lv_font_montserrat_16,
                                 COL_ROOM_NAME, room_names[idx]);
    lv_obj_set_pos(s_lbl_name[idx], CELL_CX, NAME_Y);

    s_lbl_temp[idx] = make_label(cell, &lv_font_montserrat_34,
                                 COL_UNAVAILABLE, "--");
    lv_obj_set_pos(s_lbl_temp[idx], CELL_CX, TEMP_Y);

    s_lbl_rh[idx] = make_label(cell, &lv_font_montserrat_16,
                               COL_UNAVAILABLE, "--%");
    lv_obj_set_pos(s_lbl_rh[idx], CELL_CX, RH_Y);
}

/* ── Public API ───────────────────────────────────────────────────── */
void indoor_ui_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, COL_PAGE_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    lv_obj_t *title = make_label(parent, &lv_font_montserrat_24,
                                 COL_TITLE, "Indoor climate");
    lv_obj_set_pos(title, TITLE_X, TITLE_Y);

    lv_obj_t *subtitle = make_label(parent, &lv_font_montserrat_14,
                                    COL_SUBTITLE, "Temperatuur en luchtvochtigheid per zone");
    lv_obj_set_pos(subtitle, TITLE_X, SUBTITLE_Y);

    int32_t y = GRID_Y;

    for (int row = 0; row < ROWS; row++) {
        lv_obj_t *flbl = make_label(parent, &lv_font_montserrat_16,
                                    COL_ROW_LABEL, floor_names[row]);
        lv_obj_set_style_text_letter_space(flbl, 4, 0);
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

void indoor_ui_set_zone(int index, float temp_c, float rh_pct,
                        bool temp_ok, bool rh_ok)
{
    if (index < 0 || index >= INDOOR_ZONES) return;
    if (s_accent[index] == NULL) return;

    char buf[16];

    lv_obj_set_style_bg_color(s_accent[index],
        temp_ok ? temp_to_accent(temp_c) : COL_ACCENT_NEUTRAL, 0);

    if (temp_ok) {
        snprintf(buf, sizeof(buf), "%.1f\u00B0", (double)temp_c);
        lv_label_set_text(s_lbl_temp[index], buf);
        lv_obj_set_style_text_color(s_lbl_temp[index], COL_TEMP, 0);
    } else {
        lv_label_set_text(s_lbl_temp[index], "--");
        lv_obj_set_style_text_color(s_lbl_temp[index], COL_UNAVAILABLE, 0);
    }

    if (rh_ok) {
        snprintf(buf, sizeof(buf), "%.0f%%", (double)rh_pct);
        lv_label_set_text(s_lbl_rh[index], buf);
        lv_obj_set_style_text_color(s_lbl_rh[index], COL_RH, 0);
    } else {
        lv_label_set_text(s_lbl_rh[index], "--%");
        lv_obj_set_style_text_color(s_lbl_rh[index], COL_UNAVAILABLE, 0);
    }
}
