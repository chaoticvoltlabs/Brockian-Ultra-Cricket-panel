/**
 * @file  ui_controls.c
 * @brief Page 4 — scene triggers + direct light/switch controls.
 *
 * Layout (right-side 500 × 480):
 *
 *   ┌─────────────────────────────────────────────┐
 *   │  ACTIES                                     │  section label
 *   │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐       │  4 scene buttons
 *   │  │Avond │ │Alles │ │Buiten│ │ Film │       │  (1 × 4, h=100)
 *   │  │      │ │ uit  │ │      │ │      │       │
 *   │  └──────┘ └──────┘ └──────┘ └──────┘       │
 *   │  ──────────────────────────────────────     │  separator
 *   │  BEDIENING                                  │  section label
 *   │  ┌──────────┐ ┌──────────┐ ┌──────────┐    │  6 target buttons
 *   │  │▎Hoofdlicht│ │▎Keuken   │ │▎Kastlamp │    │  (2 × 3, h=85)
 *   │  └──────────┘ └──────────┘ └──────────┘    │
 *   │  ┌──────────┐ ┌──────────┐ ┌──────────┐    │
 *   │  │▎Hal      │ │▎Bedlamp  │ │▎Ventilator│    │
 *   │  └──────────┘ └──────────┘ └──────────┘    │
 *   └─────────────────────────────────────────────┘
 *
 * Scene buttons: stateless action triggers, pressed feedback only.
 * Target buttons: on/off/unavailable state with a 2 px left accent
 * line (reuses the indoor-page accent convention).
 *
 * Touch targets are sized for wall use at 80–100 cm viewing distance.
 * The design leaves room for future dimmer interaction (long-press,
 * overlay) by keeping the event surface per button simple and uniform.
 */

#include "ui_controls.h"
#include "buc_display.h"
#include "ui_theme.h"
#include <stdint.h>

/* ── Grid geometry ─────────────────────────────────────────────────── */
#define MARGIN          10      /* left/right margin inside parent     */
#define CONTENT_W      480      /* 500 - 2 × MARGIN                   */

/* Scene buttons: 1 row × 4 */
#define SCENE_N          4
#define SCENE_GAP        8
#define SCENE_W        114      /* (480 − 3×8) / 4                    */
#define SCENE_H        100
#define SCENE_LABEL_Y   28      /* aligned with hero temp in left col  */
#define SCENE_Y         50      /* buttons top (label + 22 px gap)     */

/* Separator — aligned with the divider under the clock in the left
 * column (~Y 187).  Extra ACTIES height comes at the expense of the
 * BEDIENING row gap; row-2 bottom stays on the baro-graph baseline. */
#define SEP_Y          186

/* Target buttons: 2 rows × 3 */
#define TARGET_N         6
#define TARGET_COLS      3
#define TARGET_ROWS      2
#define TARGET_GAP       9      /* (480 − 3×154) / 2 = 9              */
#define TARGET_W       154
#define TARGET_H        85
#define TARGET_ROW_GAP  45      /* reduced to keep row-2 bottom at 439 */
#define TARGET_LABEL_Y 204
#define TARGET_Y       224      /* 224+85+45+85 = 439 → graph bottom  */
/* ── Button colours ────────────────────────────────────────────────── */
#define COL_BTN_BG         lv_color_hex(0x141A28)   /* base fill       */
#define COL_BTN_BORDER     lv_color_hex(0x2A3244)   /* subtle border   */
#define COL_BTN_PRESS_BG   lv_color_hex(0x1E2840)   /* press feedback  */
#define COL_BTN_PRESS_BD   lv_color_hex(0x3A4458)   /* press border    */
#define COL_BTN_LABEL      COL_LIGHT_GREY            /* 0xB0B8C8       */
#define COL_SECT_LABEL     lv_color_hex(0x606878)   /* section heading */

/* Target on/off accent colours */
#define COL_ON_ACCENT      lv_color_hex(0x3070B0)   /* muted blue      */
#define COL_ON_LABEL       lv_color_hex(0xD0D8E8)   /* soft white      */
#define COL_OFF_ACCENT     lv_color_hex(0x1C2030)   /* dim neutral     */

/* Target unavailable */
#define COL_DIS_BG         lv_color_hex(0x0E1220)
#define COL_DIS_BORDER     lv_color_hex(0x1C2030)
#define COL_DIS_LABEL      lv_color_hex(0x404858)

/* Button shape */
#define BTN_RADIUS          6
#define BTN_BORDER_W        1

/* Target accent line */
#define ACCENT_W            2
#define ACCENT_INSET_X      4   /* from left edge of button            */
#define ACCENT_PAD_Y        8   /* top/bottom padding within button    */

/* ── Placeholder labels ───────────────────────────────────────────── */
static const char *scene_labels[SCENE_N] = {
    "Work", "Evening", "Movie", "Night"
};

static const char *target_labels[TARGET_N] = {
    "Light A", "Light B", "Light C",
    "Media", "", ""
};

/* ── Runtime handles ──────────────────────────────────────────────── */
static lv_obj_t *s_scene_btn[SCENE_N];
static lv_obj_t *s_scene_accent[SCENE_N];
static lv_obj_t *s_scene_lbl[SCENE_N];
static lv_obj_t *s_scene_hdr;
static lv_obj_t *s_target_btn[TARGET_N];
static lv_obj_t *s_target_accent[TARGET_N];
static lv_obj_t *s_target_lbl[TARGET_N];
static lv_obj_t *s_target_hdr;
static lv_obj_t *s_sep;
static lv_obj_t *s_debug_ip_lbl;
static lv_obj_t *s_debug_mac_lbl;
static ui_controls_target_press_cb_t s_target_press_cb;
static ui_controls_scene_press_cb_t s_scene_press_cb;
static int s_active_scene = -1;
static ui_ctrl_state_t s_target_state[TARGET_N];
static bool s_scene_enabled[SCENE_N];

static lv_color_t col_btn_bg(void) { return ui_theme_is_night_mode() ? lv_color_hex(0x0D0D0D) : COL_BTN_BG; }
static lv_color_t col_btn_border(void) { return ui_theme_is_night_mode() ? lv_color_hex(0x242424) : COL_BTN_BORDER; }
static lv_color_t col_btn_press_bg(void) { return ui_theme_is_night_mode() ? lv_color_hex(0x171717) : COL_BTN_PRESS_BG; }
static lv_color_t col_btn_press_bd(void) { return ui_theme_is_night_mode() ? lv_color_hex(0x323232) : COL_BTN_PRESS_BD; }
static lv_color_t col_btn_label(void) { return ui_theme_is_night_mode() ? lv_color_hex(0x948C7F) : COL_BTN_LABEL; }
static lv_color_t col_sect_label(void) { return ui_theme_is_night_mode() ? lv_color_hex(0x5E564B) : COL_SECT_LABEL; }
static lv_color_t col_dis_bg(void) { return ui_theme_is_night_mode() ? lv_color_hex(0x060606) : COL_DIS_BG; }
static lv_color_t col_dis_border(void) { return ui_theme_is_night_mode() ? lv_color_hex(0x141414) : COL_DIS_BORDER; }
static lv_color_t col_dis_label(void) { return ui_theme_is_night_mode() ? lv_color_hex(0x35312B) : COL_DIS_LABEL; }
static lv_color_t col_separator(void) { return ui_theme_is_night_mode() ? lv_color_hex(0x181410) : COL_SEPARATOR; }

/* ── Helpers ───────────────────────────────────────────────────────── */

static lv_obj_t *section_label(lv_obj_t *parent, int32_t y,
                               const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, col_sect_label(), 0);
    lv_obj_set_style_text_letter_space(lbl, 3, 0);
    lv_label_set_text(lbl, text);
    lv_obj_set_pos(lbl, MARGIN, y);
    return lbl;
}

/** Base button: dark fill, subtle border, pressed feedback. */
static lv_obj_t *make_btn(lv_obj_t *parent, int32_t x, int32_t y,
                           int32_t w, int32_t h)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* Normal state */
    lv_obj_set_style_bg_color(btn, col_btn_bg(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, col_btn_border(), 0);
    lv_obj_set_style_border_width(btn, BTN_BORDER_W, 0);
    lv_obj_set_style_radius(btn, BTN_RADIUS, 0);

    /* Pressed state — subtle lift */
    lv_obj_set_style_bg_color(btn, col_btn_press_bg(), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, col_btn_press_bd(), LV_STATE_PRESSED);

    return btn;
}

static void target_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_target_press_cb == NULL) {
        return;
    }

    intptr_t raw = (intptr_t)lv_event_get_user_data(e);
    int idx = (int)raw;
    if (idx < 0 || idx >= TARGET_N) {
        return;
    }

    s_target_press_cb(idx);
}

static void scene_btn_event_cb(lv_event_t *e)
{
    if (s_scene_press_cb == NULL) {
        return;
    }

    intptr_t raw = (intptr_t)lv_event_get_user_data(e);
    int idx = (int)raw;
    if (idx < 0 || idx >= SCENE_N) {
        return;
    }

    switch (lv_event_get_code(e)) {
    case LV_EVENT_CLICKED:
        s_scene_press_cb(idx, UI_CTRL_PRESS_SHORT);
        break;
    case LV_EVENT_LONG_PRESSED:
        if (idx == s_active_scene) {
            s_scene_press_cb(idx, UI_CTRL_PRESS_LONG);
        }
        break;
    default:
        break;
    }
}

static void make_scene_btn(lv_obj_t *parent, int col)
{
    int32_t x = MARGIN + col * (SCENE_W + SCENE_GAP);
    lv_obj_t *btn = make_btn(parent, x, SCENE_Y, SCENE_W, SCENE_H);
    s_scene_btn[col] = btn;
    lv_obj_add_event_cb(btn, scene_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)col);
    lv_obj_add_event_cb(btn, scene_btn_event_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)col);

    lv_obj_t *acc = lv_obj_create(btn);
    lv_obj_remove_style_all(acc);
    lv_obj_clear_flag(acc,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(acc, ACCENT_INSET_X, ACCENT_PAD_Y);
    lv_obj_set_size(acc, ACCENT_W, SCENE_H - 2 * ACCENT_PAD_Y);
    lv_obj_set_style_bg_color(acc, COL_OFF_ACCENT, 0);
    lv_obj_set_style_bg_opa(acc, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(acc, 1, 0);
    s_scene_accent[col] = acc;

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, col_btn_label(), 0);
    lv_label_set_text(lbl, scene_labels[col]);
    lv_obj_center(lbl);
    s_scene_lbl[col] = lbl;
}

static void make_target_btn(lv_obj_t *parent, int row, int col)
{
    int idx = row * TARGET_COLS + col;
    int32_t x = MARGIN + col * (TARGET_W + TARGET_GAP);
    int32_t y = TARGET_Y + row * (TARGET_H + TARGET_ROW_GAP);

    lv_obj_t *btn = make_btn(parent, x, y, TARGET_W, TARGET_H);
    s_target_btn[idx] = btn;
    lv_obj_add_event_cb(btn, target_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    /* 2 px left accent — state indicator (same convention as page 2) */
    lv_obj_t *acc = lv_obj_create(btn);
    lv_obj_remove_style_all(acc);
    lv_obj_clear_flag(acc,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(acc, ACCENT_INSET_X, ACCENT_PAD_Y);
    lv_obj_set_size(acc, ACCENT_W, TARGET_H - 2 * ACCENT_PAD_Y);
    lv_obj_set_style_bg_color(acc, COL_OFF_ACCENT, 0);
    lv_obj_set_style_bg_opa(acc, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(acc, 1, 0);
    s_target_accent[idx] = acc;

    /* Label — centred in the full button area */
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, col_btn_label(), 0);
    lv_label_set_text(lbl, target_labels[idx]);
    lv_obj_center(lbl);
    s_target_lbl[idx] = lbl;
}

/* ── Public API ────────────────────────────────────────────────────── */

void ui_controls_create(lv_obj_t *parent)
{
    for (int i = 0; i < SCENE_N; i++) {
        s_scene_enabled[i] = true;
    }
    for (int i = 0; i < TARGET_N; i++) {
        s_target_state[i] = CTRL_OFF;
    }

    /* ── Top section: scene / automation triggers ────────────────── */
    s_scene_hdr = section_label(parent, SCENE_LABEL_Y, "SCENES");

    for (int i = 0; i < SCENE_N; i++) {
        make_scene_btn(parent, i);
    }

    /* ── Separator ───────────────────────────────────────────────── */
    s_sep = lv_obj_create(parent);
    lv_obj_remove_style_all(s_sep);
    lv_obj_clear_flag(s_sep,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_sep, MARGIN, SEP_Y);
    lv_obj_set_size(s_sep, CONTENT_W, 1);
    lv_obj_set_style_bg_color(s_sep, col_separator(), 0);
    lv_obj_set_style_bg_opa(s_sep, LV_OPA_COVER, 0);

    /* ── Bottom section: direct target controls ──────────────────── */
    s_target_hdr = section_label(parent, TARGET_LABEL_Y, "DIRECT CONTROL");

    for (int r = 0; r < TARGET_ROWS; r++) {
        for (int c = 0; c < TARGET_COLS; c++) {
            make_target_btn(parent, r, c);
        }
    }

    s_debug_ip_lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(s_debug_ip_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_debug_ip_lbl, col_sect_label(), 0);
    lv_label_set_text(s_debug_ip_lbl, "");
    lv_obj_align(s_debug_ip_lbl, LV_ALIGN_BOTTOM_RIGHT, -10, -14);

    s_debug_mac_lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(s_debug_mac_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_debug_mac_lbl, col_sect_label(), 0);
    lv_label_set_text(s_debug_mac_lbl, "");
    lv_obj_align(s_debug_mac_lbl, LV_ALIGN_BOTTOM_RIGHT, -10, 0);
}

void ui_controls_set_target_press_cb(ui_controls_target_press_cb_t cb)
{
    s_target_press_cb = cb;
}

void ui_controls_set_scene_press_cb(ui_controls_scene_press_cb_t cb)
{
    s_scene_press_cb = cb;
}

void ui_controls_set_active_scene(int index)
{
    s_active_scene = (index >= 0 && index < SCENE_N) ? index : -1;

    for (int i = 0; i < SCENE_N; i++) {
        bool active = (i == s_active_scene);
        lv_obj_set_style_bg_color(s_scene_accent[i],
            active ? COL_ON_ACCENT : COL_OFF_ACCENT, 0);
        lv_obj_set_style_text_color(s_scene_lbl[i],
            active ? COL_ON_LABEL : col_btn_label(), 0);
    }
}

void ui_controls_set_scene_slot(int index, const char *label, bool enabled)
{
    if (index < 0 || index >= SCENE_N) return;
    s_scene_enabled[index] = enabled;

    lv_label_set_text(s_scene_lbl[index], (label != NULL) ? label : "");
    if (enabled) {
        lv_obj_add_flag(s_scene_btn[index], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(s_scene_btn[index], col_btn_bg(), 0);
        lv_obj_set_style_border_color(s_scene_btn[index], col_btn_border(), 0);
        lv_obj_set_style_text_color(s_scene_lbl[index], col_btn_label(), 0);
    } else {
        lv_obj_clear_flag(s_scene_btn[index], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(s_scene_btn[index], col_dis_bg(), 0);
        lv_obj_set_style_border_color(s_scene_btn[index], col_dis_border(), 0);
        lv_obj_set_style_text_color(s_scene_lbl[index], col_dis_label(), 0);
        lv_obj_set_style_bg_color(s_scene_accent[index], COL_OFF_ACCENT, 0);
    }
}

void ui_controls_set_debug_identity(const char *ipv4, const char *mac)
{
    char buf[48];

    if (s_debug_ip_lbl != NULL) {
        if (ipv4 != NULL && ipv4[0] != '\0') {
            lv_snprintf(buf, sizeof(buf), "IP %s", ipv4);
            lv_label_set_text(s_debug_ip_lbl, buf);
        } else {
            lv_label_set_text(s_debug_ip_lbl, "");
        }
    }
    if (s_debug_mac_lbl != NULL) {
        if (mac != NULL && mac[0] != '\0') {
            lv_snprintf(buf, sizeof(buf), "MAC %s", mac);
            lv_label_set_text(s_debug_mac_lbl, buf);
        } else {
            lv_label_set_text(s_debug_mac_lbl, "");
        }
    }
}

void ui_controls_set_target_label(int index, const char *label)
{
    if (index < 0 || index >= TARGET_N) return;
    lv_label_set_text(s_target_lbl[index], (label != NULL) ? label : "");
}

void ui_controls_set_target_state(int index, ui_ctrl_state_t state)
{
    if (index < 0 || index >= TARGET_N) return;
    s_target_state[index] = state;

    lv_obj_t *btn = s_target_btn[index];
    lv_obj_t *acc = s_target_accent[index];
    lv_obj_t *lbl = s_target_lbl[index];

    switch (state) {
    case CTRL_ON:
        lv_obj_set_style_bg_color(acc, COL_ON_ACCENT, 0);
        lv_obj_set_style_text_color(lbl, COL_ON_LABEL, 0);
        lv_obj_set_style_bg_color(btn, col_btn_bg(), 0);
        lv_obj_set_style_border_color(btn, col_btn_border(), 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        break;

    case CTRL_OFF:
        lv_obj_set_style_bg_color(acc, COL_OFF_ACCENT, 0);
        lv_obj_set_style_text_color(lbl, col_btn_label(), 0);
        lv_obj_set_style_bg_color(btn, col_btn_bg(), 0);
        lv_obj_set_style_border_color(btn, col_btn_border(), 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        break;

    case CTRL_UNAVAILABLE:
        lv_obj_set_style_bg_color(acc, COL_OFF_ACCENT, 0);
        lv_obj_set_style_text_color(lbl, col_dis_label(), 0);
        lv_obj_set_style_bg_color(btn, col_dis_bg(), 0);
        lv_obj_set_style_border_color(btn, col_dis_border(), 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        break;
    }
}

ui_ctrl_state_t ui_controls_get_target_state(int index)
{
    if (index < 0 || index >= TARGET_N) {
        return CTRL_UNAVAILABLE;
    }

    return s_target_state[index];
}

void ui_controls_apply_theme(void)
{
    if (s_scene_hdr == NULL) {
        return;
    }

    lv_obj_set_style_text_color(s_scene_hdr, col_sect_label(), 0);
    lv_obj_set_style_text_color(s_target_hdr, col_sect_label(), 0);
    lv_obj_set_style_text_color(s_debug_ip_lbl, col_sect_label(), 0);
    lv_obj_set_style_text_color(s_debug_mac_lbl, col_sect_label(), 0);
    lv_obj_set_style_bg_color(s_sep, col_separator(), 0);

    for (int i = 0; i < SCENE_N; i++) {
        lv_obj_set_style_bg_color(s_scene_btn[i], col_btn_bg(), 0);
        lv_obj_set_style_border_color(s_scene_btn[i], col_btn_border(), 0);
        lv_obj_set_style_bg_color(s_scene_btn[i], col_btn_press_bg(), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(s_scene_btn[i], col_btn_press_bd(), LV_STATE_PRESSED);
        ui_controls_set_scene_slot(i, lv_label_get_text(s_scene_lbl[i]), s_scene_enabled[i]);
    }
    ui_controls_set_active_scene(s_active_scene);

    for (int i = 0; i < TARGET_N; i++) {
        lv_obj_set_style_bg_color(s_target_btn[i], col_btn_bg(), 0);
        lv_obj_set_style_border_color(s_target_btn[i], col_btn_border(), 0);
        lv_obj_set_style_bg_color(s_target_btn[i], col_btn_press_bg(), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(s_target_btn[i], col_btn_press_bd(), LV_STATE_PRESSED);
        ui_controls_set_target_state(i, s_target_state[i]);
    }
}
