#include "control_ui.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "panel_alert.h"
#include "control_post.h"

#define CTRL_BG      lv_color_hex(0x090909)
#define CTRL_PANEL   lv_color_hex(0x121212)
#define CTRL_EDGE    lv_color_hex(0x262626)
#define CTRL_TEXT    lv_color_hex(0xC8BA94)
#define CTRL_SUBTLE  lv_color_hex(0x8B836F)
#define CTRL_ACCENT  lv_color_hex(0xC1962E)
#define CTRL_SOFT    lv_color_hex(0x6E6658)
#define CTRL_GREEN   lv_color_hex(0x6B8D61)

typedef struct {
    const char *label;
    const char *target;
    const char *action;
    bool        initial;
} slot_meta_t;

typedef struct {
    lv_obj_t *label;
    const char *suffix;
} slider_label_ctx_t;

static const slot_meta_t s_scenes[] = {
    { "Relax", "ambient_relax", "activate", true  },
    { "Work",  "ambient_work",  "activate", false },
    { "Movie", "ambient_movie", "activate", false },
    { "Night", "ambient_night", "activate", false },
};

static const slot_meta_t s_targets[CTRL_TARGETS_MAX] = {
    { "Accent",     "accent",      "toggle",   true  },
    { "Ambient",    "ambient",     "toggle",   false },
    { "Desk",       "desk",        "toggle",   false },
    { "Shelf",      "shelf",       "toggle",   true  },
    { "Media",      "media_power", "toggle",   true  },
    { "Vent",       "vent",        "toggle",   false },
};

static const slot_meta_t s_media_actions[] = {
    { "Media start", "media_start", "activate", true  },
    { "Intermission","media_break", "activate", false },
    { "Media off",   "media_off",   "activate", false },
};

static lv_obj_t *s_status_lbl;
static lv_obj_t *s_target_sw[CTRL_TARGETS_MAX];
static lv_obj_t *s_target_lbl[CTRL_TARGETS_MAX];
static lv_obj_t *s_debug_ip_lbl;
static lv_obj_t *s_debug_mac_lbl;
static lv_obj_t *s_ambient_brightness_slider;
static lv_obj_t *s_ambient_brightness_value;
static lv_obj_t *s_ambient_color_slider;
static lv_obj_t *s_ambient_color_value;
static bool s_syncing_ambient_controls;

static void slot_click_cb(lv_event_t *e)
{
    const slot_meta_t *m = lv_event_get_user_data(e);
    if (m != NULL) {
        control_post_enqueue(m->target, m->action);
    }
}

static void style_text(lv_obj_t *obj, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
}

static lv_obj_t *create_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, CTRL_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, CTRL_EDGE, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void slider_value_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    slider_label_ctx_t *ctx = lv_event_get_user_data(e);
    if (!ctx || !ctx->label) return;

    char buf[24];
    snprintf(buf, sizeof(buf), "%d%s", (int)lv_slider_get_value(slider), ctx->suffix);
    lv_label_set_text(ctx->label, buf);
}

static void ambient_brightness_commit_cb(lv_event_t *e)
{
    if (s_syncing_ambient_controls) return;
    lv_obj_t *slider = lv_event_get_target(e);
    control_post_enqueue_value("ambient", "set_brightness", (int)lv_slider_get_value(slider));
}

static void ambient_hue_to_rgb(int hue_deg, int *r, int *g, int *b)
{
    float h = fmodf((float)hue_deg, 360.0f);
    if (h < 0.0f) h += 360.0f;
    float c = 1.0f;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float rf = 0.0f, gf = 0.0f, bf = 0.0f;

    if (h < 60.0f) {
        rf = c; gf = x;
    } else if (h < 120.0f) {
        rf = x; gf = c;
    } else if (h < 180.0f) {
        gf = c; bf = x;
    } else if (h < 240.0f) {
        gf = x; bf = c;
    } else if (h < 300.0f) {
        rf = x; bf = c;
    } else {
        rf = c; bf = x;
    }

    *r = (int)lroundf(rf * 255.0f);
    *g = (int)lroundf(gf * 255.0f);
    *b = (int)lroundf(bf * 255.0f);
}

static int ambient_rgb_to_hue(int r, int g, int b)
{
    float rf = (float)r / 255.0f;
    float gf = (float)g / 255.0f;
    float bf = (float)b / 255.0f;
    float maxv = fmaxf(rf, fmaxf(gf, bf));
    float minv = fminf(rf, fminf(gf, bf));
    float delta = maxv - minv;
    if (delta <= 0.0001f) return 0;

    float hue;
    if (maxv == rf) {
        hue = 60.0f * fmodf(((gf - bf) / delta), 6.0f);
    } else if (maxv == gf) {
        hue = 60.0f * (((bf - rf) / delta) + 2.0f);
    } else {
        hue = 60.0f * (((rf - gf) / delta) + 4.0f);
    }
    if (hue < 0.0f) hue += 360.0f;
    return (int)lroundf(hue);
}

static void ambient_color_label_set(int r, int g, int b)
{
    if (s_ambient_color_value == NULL) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    lv_label_set_text(s_ambient_color_value, buf);
    lv_obj_set_style_text_color(s_ambient_color_value, lv_color_make(r, g, b), 0);
}

static void ambient_color_preview_cb(lv_event_t *e)
{
    if (s_ambient_color_value == NULL) return;
    int hue = (int)lv_slider_get_value(lv_event_get_target(e));
    int r = 0, g = 0, b = 0;
    ambient_hue_to_rgb(hue, &r, &g, &b);
    ambient_color_label_set(r, g, b);
}

static void ambient_color_commit_cb(lv_event_t *e)
{
    if (s_syncing_ambient_controls) return;
    int hue = (int)lv_slider_get_value(lv_event_get_target(e));
    int r = 0, g = 0, b = 0;
    ambient_hue_to_rgb(hue, &r, &g, &b);
    control_post_enqueue_rgb("ambient", "set_rgb", r, g, b);
}

static lv_obj_t *create_scene_chip(lv_obj_t *parent, const slot_meta_t *meta, lv_coord_t x, lv_coord_t y)
{
    bool active = meta->initial;

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 160, 54);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, 27, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, active ? CTRL_ACCENT : CTRL_EDGE, 0);
    lv_obj_set_style_bg_color(btn, active ? CTRL_ACCENT : CTRL_PANEL, 0);
    lv_obj_set_style_bg_opa(btn, active ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, slot_click_cb, LV_EVENT_CLICKED, (void *)meta);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, meta->label);
    style_text(label, &lv_font_montserrat_18, active ? CTRL_TEXT : CTRL_SUBTLE);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *create_switch_row(lv_obj_t *parent, int index, const slot_meta_t *meta, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, meta->label);
    style_text(label, &lv_font_montserrat_18, CTRL_TEXT);
    lv_obj_set_pos(label, 0, y);

    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_pos(sw, 178, y - 2);
    lv_obj_set_style_bg_color(sw, CTRL_SOFT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, CTRL_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_70, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, CTRL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    if (meta->initial) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, slot_click_cb, LV_EVENT_VALUE_CHANGED, (void *)meta);

    if (index >= 0 && index < CTRL_TARGETS_MAX) {
        s_target_sw[index] = sw;
        s_target_lbl[index] = label;
    }

    return sw;
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const slot_meta_t *meta, lv_coord_t x)
{
    bool primary = meta->initial;

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 170, 62);
    lv_obj_set_pos(btn, x, 82);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    if (primary) {
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, CTRL_ACCENT, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_40, 0);
    } else {
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, CTRL_EDGE, 0);
        lv_obj_set_style_bg_color(btn, CTRL_PANEL, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    }

    lv_obj_add_event_cb(btn, slot_click_cb, LV_EVENT_CLICKED, (void *)meta);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, meta->label);
    style_text(label, &lv_font_montserrat_18, primary ? CTRL_TEXT : CTRL_SUBTLE);
    lv_obj_center(label);
    return btn;
}

void control_ui_create(lv_obj_t *parent)
{
    static slider_label_ctx_t brightness_ctx;

    lv_obj_set_style_bg_color(parent, CTRL_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Showcase control");
    style_text(title, &lv_font_montserrat_28, CTRL_TEXT);
    lv_obj_set_pos(title, 40, 44);

    panel_alert_create_storm(parent, 344, 42);

    s_status_lbl = lv_label_create(parent);
    lv_label_set_text(s_status_lbl, "--.- C  |  --% RH");
    style_text(s_status_lbl, &lv_font_montserrat_24, CTRL_GREEN);
    lv_obj_align_to(s_status_lbl, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 16);

    lv_obj_t *scene_title = lv_label_create(parent);
    lv_label_set_text(scene_title, "Scenes");
    style_text(scene_title, &lv_font_montserrat_18, CTRL_SUBTLE);
    lv_obj_set_pos(scene_title, 40, 150);

    create_scene_chip(parent, &s_scenes[0], 40,  184);
    create_scene_chip(parent, &s_scenes[1], 216, 184);
    create_scene_chip(parent, &s_scenes[2], 40,  252);
    create_scene_chip(parent, &s_scenes[3], 216, 252);

    lv_obj_t *ambience = create_card(parent, 40, 336, 336, 196);

    lv_obj_t *amb_title = lv_label_create(ambience);
    lv_label_set_text(amb_title, "Ambience");
    style_text(amb_title, &lv_font_montserrat_20, CTRL_TEXT);
    lv_obj_set_pos(amb_title, 0, 0);

    lv_obj_t *bright_label = lv_label_create(ambience);
    lv_label_set_text(bright_label, "Brightness");
    style_text(bright_label, &lv_font_montserrat_16, CTRL_SUBTLE);
    lv_obj_set_pos(bright_label, 0, 48);

    s_ambient_brightness_value = lv_label_create(ambience);
    lv_label_set_text(s_ambient_brightness_value, "68%");
    style_text(s_ambient_brightness_value, &lv_font_montserrat_16, CTRL_TEXT);
    lv_obj_align(s_ambient_brightness_value, LV_ALIGN_TOP_RIGHT, 0, 48);

    s_ambient_brightness_slider = lv_slider_create(ambience);
    lv_obj_set_size(s_ambient_brightness_slider, 300, 6);
    lv_obj_set_pos(s_ambient_brightness_slider, 0, 82);
    lv_slider_set_range(s_ambient_brightness_slider, 0, 100);
    lv_slider_set_value(s_ambient_brightness_slider, 68, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ambient_brightness_slider, CTRL_EDGE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ambient_brightness_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ambient_brightness_slider, CTRL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ambient_brightness_slider, LV_OPA_70, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ambient_brightness_slider, CTRL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_ambient_brightness_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_ambient_brightness_slider, 4, LV_PART_KNOB);

    brightness_ctx.label = s_ambient_brightness_value;
    brightness_ctx.suffix = "%";
    lv_obj_add_event_cb(s_ambient_brightness_slider, slider_value_cb, LV_EVENT_VALUE_CHANGED, &brightness_ctx);
    lv_obj_add_event_cb(s_ambient_brightness_slider, ambient_brightness_commit_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *warmth_label = lv_label_create(ambience);
    lv_label_set_text(warmth_label, "Color");
    style_text(warmth_label, &lv_font_montserrat_16, CTRL_SUBTLE);
    lv_obj_set_pos(warmth_label, 0, 118);

    s_ambient_color_value = lv_label_create(ambience);
    lv_label_set_text(s_ambient_color_value, "#FF9900");
    style_text(s_ambient_color_value, &lv_font_montserrat_16, CTRL_TEXT);
    lv_obj_align(s_ambient_color_value, LV_ALIGN_TOP_RIGHT, 0, 118);

    s_ambient_color_slider = lv_slider_create(ambience);
    lv_obj_set_size(s_ambient_color_slider, 300, 6);
    lv_obj_set_pos(s_ambient_color_slider, 0, 152);
    lv_slider_set_range(s_ambient_color_slider, 0, 359);
    lv_slider_set_value(s_ambient_color_slider, 36, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ambient_color_slider, CTRL_EDGE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ambient_color_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ambient_color_slider, CTRL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ambient_color_slider, LV_OPA_70, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ambient_color_slider, CTRL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_ambient_color_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_ambient_color_slider, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(s_ambient_color_slider, ambient_color_preview_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_ambient_color_slider, ambient_color_commit_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *lights = create_card(parent, 416, 44, 260, 220);
    lv_obj_t *lights_title = lv_label_create(lights);
    lv_label_set_text(lights_title, "Lighting");
    style_text(lights_title, &lv_font_montserrat_24, CTRL_TEXT);
    lv_obj_set_pos(lights_title, 0, 0);

    create_switch_row(lights, 0, &s_targets[0], 56);
    create_switch_row(lights, 1, &s_targets[1], 102);
    create_switch_row(lights, 2, &s_targets[2], 148);

    lv_obj_t *lab = create_card(parent, 694, 44, 290, 220);
    lv_obj_t *lab_title = lv_label_create(lab);
    lv_label_set_text(lab_title, "Electronics lab");
    style_text(lab_title, &lv_font_montserrat_24, CTRL_TEXT);
    lv_obj_set_pos(lab_title, 0, 0);

    create_switch_row(lab, 3, &s_targets[3], 56);
    create_switch_row(lab, 4, &s_targets[4], 102);
    create_switch_row(lab, 5, &s_targets[5], 148);

    lv_obj_t *media = create_card(parent, 416, 284, 568, 248);
    lv_obj_t *media_title = lv_label_create(media);
    lv_label_set_text(media_title, "Media and power");
    style_text(media_title, &lv_font_montserrat_24, CTRL_TEXT);
    lv_obj_set_pos(media_title, 0, 0);

    lv_obj_t *media_status = lv_label_create(media);
    lv_label_set_text(media_status, "Media scenes and room power");
    style_text(media_status, &lv_font_montserrat_16, CTRL_SUBTLE);
    lv_obj_set_pos(media_status, 0, 34);

    create_action_button(media, &s_media_actions[0], 0);
    create_action_button(media, &s_media_actions[1], 194);
    create_action_button(media, &s_media_actions[2], 388);

    s_debug_ip_lbl = lv_label_create(parent);
    style_text(s_debug_ip_lbl, &lv_font_montserrat_14, CTRL_SUBTLE);
    lv_label_set_text(s_debug_ip_lbl, "");
    lv_obj_align(s_debug_ip_lbl, LV_ALIGN_BOTTOM_RIGHT, -22, -18);

    s_debug_mac_lbl = lv_label_create(parent);
    style_text(s_debug_mac_lbl, &lv_font_montserrat_14, CTRL_SUBTLE);
    lv_label_set_text(s_debug_mac_lbl, "");
    lv_obj_align(s_debug_mac_lbl, LV_ALIGN_BOTTOM_RIGHT, -22, 0);
}

void control_ui_set_debug_identity(const char *ipv4, const char *mac)
{
    char buf[48];

    if (s_debug_ip_lbl != NULL) {
        if (ipv4 != NULL && ipv4[0] != '\0') {
            snprintf(buf, sizeof(buf), "IP %s", ipv4);
            lv_label_set_text(s_debug_ip_lbl, buf);
        } else {
            lv_label_set_text(s_debug_ip_lbl, "");
        }
    }

    if (s_debug_mac_lbl != NULL) {
        if (mac != NULL && mac[0] != '\0') {
            snprintf(buf, sizeof(buf), "MAC %s", mac);
            lv_label_set_text(s_debug_mac_lbl, buf);
        } else {
            lv_label_set_text(s_debug_mac_lbl, "");
        }
    }
}

void control_ui_set_room_climate(float temp_c, bool temp_ok, float rh_pct, bool rh_ok)
{
    char buf[40];

    if (s_status_lbl == NULL) {
        return;
    }

    if (temp_ok && rh_ok) {
        snprintf(buf, sizeof(buf), "%.1f C  |  %.0f%% RH", temp_c, rh_pct);
    } else if (temp_ok) {
        snprintf(buf, sizeof(buf), "%.1f C  |  --%% RH", temp_c);
    } else if (rh_ok) {
        snprintf(buf, sizeof(buf), "--.- C  |  %.0f%% RH", rh_pct);
    } else {
        snprintf(buf, sizeof(buf), "--.- C  |  --%% RH");
    }

    static char s_last_buf[40] = "";
    if (strcmp(buf, s_last_buf) == 0) return;
    strncpy(s_last_buf, buf, sizeof(s_last_buf) - 1);

    lv_label_set_text(s_status_lbl, buf);
}

void control_ui_set_target_state(int index, ui_ctrl_state_t state)
{
    if (index < 0 || index >= CTRL_TARGETS_MAX || s_target_sw[index] == NULL || s_target_lbl[index] == NULL) {
        return;
    }

    switch (state) {
    case CTRL_ON:
        lv_obj_clear_state(s_target_sw[index], LV_STATE_DISABLED);
        lv_obj_add_state(s_target_sw[index], LV_STATE_CHECKED);
        lv_obj_set_style_text_color(s_target_lbl[index], CTRL_TEXT, 0);
        lv_obj_set_style_bg_opa(s_target_sw[index], LV_OPA_COVER, LV_PART_MAIN);
        break;
    case CTRL_OFF:
        lv_obj_clear_state(s_target_sw[index], LV_STATE_DISABLED | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(s_target_lbl[index], CTRL_TEXT, 0);
        lv_obj_set_style_bg_opa(s_target_sw[index], LV_OPA_COVER, LV_PART_MAIN);
        break;
    case CTRL_UNAVAILABLE:
        lv_obj_clear_state(s_target_sw[index], LV_STATE_CHECKED);
        lv_obj_add_state(s_target_sw[index], LV_STATE_DISABLED);
        lv_obj_set_style_text_color(s_target_lbl[index], CTRL_SUBTLE, 0);
        lv_obj_set_style_bg_opa(s_target_sw[index], LV_OPA_30, LV_PART_MAIN);
        break;
    }
}

void control_ui_set_target_state_by_name(const char *target, ui_ctrl_state_t state)
{
    if (target == NULL) {
        return;
    }
    for (size_t i = 0; i < CTRL_TARGETS_MAX; ++i) {
        if (s_targets[i].target != NULL && strcmp(s_targets[i].target, target) == 0) {
            control_ui_set_target_state((int)i, state);
            return;
        }
    }
}

void control_ui_set_ambient_brightness(int pct)
{
    if (s_ambient_brightness_slider == NULL || s_ambient_brightness_value == NULL) {
        return;
    }
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    s_syncing_ambient_controls = true;
    lv_slider_set_value(s_ambient_brightness_slider, pct, LV_ANIM_OFF);
    s_syncing_ambient_controls = false;

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(s_ambient_brightness_value, buf);
}

void control_ui_set_ambient_rgb(int r, int g, int b)
{
    if (s_ambient_color_slider == NULL) {
        return;
    }
    if (r < 0) {
        r = 0;
    }
    if (r > 255) {
        r = 255;
    }
    if (g < 0) {
        g = 0;
    }
    if (g > 255) {
        g = 255;
    }
    if (b < 0) {
        b = 0;
    }
    if (b > 255) {
        b = 255;
    }

    int hue = ambient_rgb_to_hue(r, g, b);
    s_syncing_ambient_controls = true;
    lv_slider_set_value(s_ambient_color_slider, hue, LV_ANIM_OFF);
    s_syncing_ambient_controls = false;
    ambient_color_label_set(r, g, b);
}
