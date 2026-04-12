#include "control_ui.h"

#include <stdbool.h>
#include <stdio.h>

#define CTRL_BG      lv_color_hex(0x090909)
#define CTRL_PANEL   lv_color_hex(0x121212)
#define CTRL_EDGE    lv_color_hex(0x262626)
#define CTRL_TEXT    lv_color_hex(0xC8BA94)
#define CTRL_SUBTLE  lv_color_hex(0x8B836F)
#define CTRL_ACCENT  lv_color_hex(0xC1962E)
#define CTRL_SOFT    lv_color_hex(0x6E6658)
#define CTRL_GREEN   lv_color_hex(0x6B8D61)

typedef struct {
    lv_obj_t *label;
    const char *suffix;
} slider_label_ctx_t;

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

static lv_obj_t *create_scene_chip(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, bool active)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 160, 54);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, 27, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, active ? CTRL_ACCENT : CTRL_EDGE, 0);
    lv_obj_set_style_bg_color(btn, active ? CTRL_ACCENT : CTRL_PANEL, 0);
    lv_obj_set_style_bg_opa(btn, active ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    style_text(label, &lv_font_montserrat_18, active ? CTRL_TEXT : CTRL_SUBTLE);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *create_switch_row(lv_obj_t *parent, const char *label_text, lv_coord_t y, bool checked)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, label_text);
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
    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    return sw;
}

void control_ui_create(lv_obj_t *parent)
{
    static slider_label_ctx_t brightness_ctx;
    static slider_label_ctx_t temp_ctx;

    lv_obj_set_style_bg_color(parent, CTRL_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Mancave control");
    style_text(title, &lv_font_montserrat_28, CTRL_TEXT);
    lv_obj_set_pos(title, 40, 44);

    lv_obj_t *subtitle = lv_label_create(parent);
    lv_label_set_text(subtitle, "Lights, climate and media");
    style_text(subtitle, &lv_font_montserrat_18, CTRL_SUBTLE);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    lv_obj_t *status = lv_label_create(parent);
    lv_label_set_text(status, "Occupied  |  21.4 C  |  43% RH");
    style_text(status, &lv_font_montserrat_18, CTRL_GREEN);
    lv_obj_align_to(status, subtitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 28);

    lv_obj_t *scene_title = lv_label_create(parent);
    lv_label_set_text(scene_title, "Scenes");
    style_text(scene_title, &lv_font_montserrat_18, CTRL_SUBTLE);
    lv_obj_set_pos(scene_title, 40, 170);

    create_scene_chip(parent, "Relax", 40, 204, true);
    create_scene_chip(parent, "Work", 216, 204, false);
    create_scene_chip(parent, "Movie", 40, 272, false);
    create_scene_chip(parent, "Night", 216, 272, false);

    lv_obj_t *ambience = create_card(parent, 40, 356, 336, 176);

    lv_obj_t *amb_title = lv_label_create(ambience);
    lv_label_set_text(amb_title, "Ambience");
    style_text(amb_title, &lv_font_montserrat_20, CTRL_TEXT);
    lv_obj_set_pos(amb_title, 0, 0);

    lv_obj_t *bright_label = lv_label_create(ambience);
    lv_label_set_text(bright_label, "Brightness");
    style_text(bright_label, &lv_font_montserrat_16, CTRL_SUBTLE);
    lv_obj_set_pos(bright_label, 0, 48);

    lv_obj_t *bright_value = lv_label_create(ambience);
    lv_label_set_text(bright_value, "68%");
    style_text(bright_value, &lv_font_montserrat_16, CTRL_TEXT);
    lv_obj_align(bright_value, LV_ALIGN_TOP_RIGHT, 0, 48);

    lv_obj_t *bright_slider = lv_slider_create(ambience);
    lv_obj_set_size(bright_slider, 300, 6);
    lv_obj_set_pos(bright_slider, 0, 82);
    lv_slider_set_range(bright_slider, 0, 100);
    lv_slider_set_value(bright_slider, 68, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bright_slider, CTRL_EDGE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bright_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bright_slider, CTRL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bright_slider, LV_OPA_70, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bright_slider, CTRL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(bright_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(bright_slider, 4, LV_PART_KNOB);

    brightness_ctx.label = bright_value;
    brightness_ctx.suffix = "%";
    lv_obj_add_event_cb(bright_slider, slider_value_cb, LV_EVENT_VALUE_CHANGED, &brightness_ctx);

    lv_obj_t *warmth_label = lv_label_create(ambience);
    lv_label_set_text(warmth_label, "Warm white");
    style_text(warmth_label, &lv_font_montserrat_16, CTRL_SUBTLE);
    lv_obj_set_pos(warmth_label, 0, 118);

    lv_obj_t *warmth_value = lv_label_create(ambience);
    lv_label_set_text(warmth_value, "3200K");
    style_text(warmth_value, &lv_font_montserrat_16, CTRL_TEXT);
    lv_obj_align(warmth_value, LV_ALIGN_TOP_RIGHT, 0, 118);

    lv_obj_t *warmth_bar = lv_obj_create(ambience);
    lv_obj_set_size(warmth_bar, 300, 8);
    lv_obj_set_pos(warmth_bar, 0, 150);
    lv_obj_set_style_bg_color(warmth_bar, CTRL_EDGE, 0);
    lv_obj_set_style_bg_opa(warmth_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(warmth_bar, 0, 0);
    lv_obj_set_style_radius(warmth_bar, 4, 0);
    lv_obj_set_style_pad_all(warmth_bar, 0, 0);
    lv_obj_set_style_shadow_width(warmth_bar, 0, 0);
    lv_obj_clear_flag(warmth_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *warmth_fill = lv_obj_create(warmth_bar);
    lv_obj_set_size(warmth_fill, 180, 8);
    lv_obj_set_pos(warmth_fill, 0, 0);
    lv_obj_set_style_bg_color(warmth_fill, CTRL_ACCENT, 0);
    lv_obj_set_style_bg_opa(warmth_fill, LV_OPA_60, 0);
    lv_obj_set_style_border_width(warmth_fill, 0, 0);
    lv_obj_set_style_radius(warmth_fill, 4, 0);
    lv_obj_set_style_pad_all(warmth_fill, 0, 0);
    lv_obj_set_style_shadow_width(warmth_fill, 0, 0);
    lv_obj_clear_flag(warmth_fill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lights = create_card(parent, 416, 44, 260, 220);
    lv_obj_t *lights_title = lv_label_create(lights);
    lv_label_set_text(lights_title, "Lighting");
    style_text(lights_title, &lv_font_montserrat_24, CTRL_TEXT);
    lv_obj_set_pos(lights_title, 0, 0);

    create_switch_row(lights, "Desk strip", 56, true);
    create_switch_row(lights, "Wall spots", 102, false);
    create_switch_row(lights, "Shelf glow", 148, true);

    lv_obj_t *climate = create_card(parent, 694, 44, 290, 220);
    lv_obj_t *climate_title = lv_label_create(climate);
    lv_label_set_text(climate_title, "Climate");
    style_text(climate_title, &lv_font_montserrat_24, CTRL_TEXT);
    lv_obj_set_pos(climate_title, 0, 0);

    lv_obj_t *target_label = lv_label_create(climate);
    lv_label_set_text(target_label, "Target");
    style_text(target_label, &lv_font_montserrat_16, CTRL_SUBTLE);
    lv_obj_set_pos(target_label, 0, 54);

    lv_obj_t *target_value = lv_label_create(climate);
    lv_label_set_text(target_value, "21 C");
    style_text(target_value, &lv_font_montserrat_16, CTRL_TEXT);
    lv_obj_align(target_value, LV_ALIGN_TOP_RIGHT, 0, 54);

    lv_obj_t *temp_slider = lv_slider_create(climate);
    lv_obj_set_size(temp_slider, 254, 6);
    lv_obj_set_pos(temp_slider, 0, 88);
    lv_slider_set_range(temp_slider, 16, 24);
    lv_slider_set_value(temp_slider, 21, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(temp_slider, CTRL_EDGE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(temp_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(temp_slider, CTRL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(temp_slider, LV_OPA_70, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(temp_slider, CTRL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(temp_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(temp_slider, 4, LV_PART_KNOB);

    temp_ctx.label = target_value;
    temp_ctx.suffix = " C";
    lv_obj_add_event_cb(temp_slider, slider_value_cb, LV_EVENT_VALUE_CHANGED, &temp_ctx);

    create_switch_row(climate, "Vent boost", 126, false);
    create_switch_row(climate, "Heater", 172, true);

    lv_obj_t *media = create_card(parent, 416, 284, 568, 248);
    lv_obj_t *media_title = lv_label_create(media);
    lv_label_set_text(media_title, "Media and power");
    style_text(media_title, &lv_font_montserrat_24, CTRL_TEXT);
    lv_obj_set_pos(media_title, 0, 0);

    lv_obj_t *media_status = lv_label_create(media);
    lv_label_set_text(media_status, "Amp on  |  TV standby  |  PC sleeping");
    style_text(media_status, &lv_font_montserrat_16, CTRL_SUBTLE);
    lv_obj_set_pos(media_status, 0, 34);

    lv_obj_t *movie_btn = lv_btn_create(media);
    lv_obj_set_size(movie_btn, 170, 62);
    lv_obj_set_pos(movie_btn, 0, 82);
    lv_obj_set_style_radius(movie_btn, 16, 0);
    lv_obj_set_style_border_width(movie_btn, 0, 0);
    lv_obj_set_style_bg_color(movie_btn, CTRL_ACCENT, 0);
    lv_obj_set_style_bg_opa(movie_btn, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(movie_btn, 0, 0);
    lv_obj_t *movie_label = lv_label_create(movie_btn);
    lv_label_set_text(movie_label, "Start movie");
    style_text(movie_label, &lv_font_montserrat_18, CTRL_TEXT);
    lv_obj_center(movie_label);

    lv_obj_t *air_btn = lv_btn_create(media);
    lv_obj_set_size(air_btn, 170, 62);
    lv_obj_set_pos(air_btn, 194, 82);
    lv_obj_set_style_radius(air_btn, 16, 0);
    lv_obj_set_style_border_width(air_btn, 1, 0);
    lv_obj_set_style_border_color(air_btn, CTRL_EDGE, 0);
    lv_obj_set_style_bg_color(air_btn, CTRL_PANEL, 0);
    lv_obj_set_style_bg_opa(air_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(air_btn, 0, 0);
    lv_obj_t *air_label = lv_label_create(air_btn);
    lv_label_set_text(air_label, "Air out");
    style_text(air_label, &lv_font_montserrat_18, CTRL_SUBTLE);
    lv_obj_center(air_label);

    lv_obj_t *off_btn = lv_btn_create(media);
    lv_obj_set_size(off_btn, 170, 62);
    lv_obj_set_pos(off_btn, 388, 82);
    lv_obj_set_style_radius(off_btn, 16, 0);
    lv_obj_set_style_border_width(off_btn, 1, 0);
    lv_obj_set_style_border_color(off_btn, CTRL_EDGE, 0);
    lv_obj_set_style_bg_color(off_btn, CTRL_PANEL, 0);
    lv_obj_set_style_bg_opa(off_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(off_btn, 0, 0);
    lv_obj_t *off_label = lv_label_create(off_btn);
    lv_label_set_text(off_label, "All off");
    style_text(off_label, &lv_font_montserrat_18, CTRL_SUBTLE);
    lv_obj_center(off_label);

    lv_obj_t *footer = lv_label_create(media);
    lv_label_set_text(footer, "Touch-first placeholder page for device wiring and scene logic.");
    style_text(footer, &lv_font_montserrat_14, CTRL_SUBTLE);
    lv_obj_set_pos(footer, 0, 184);
}
