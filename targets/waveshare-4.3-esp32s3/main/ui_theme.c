#include "ui_theme.h"

#include "ui_weather.h"
#include "ui_compass.h"
#include "ui_clock.h"
#include "ui_controls.h"
#include "ui_indoor.h"
#include "ui_wind_strip.h"

static bool s_night_mode = false;

void ui_theme_set_night_mode(bool enabled)
{
    s_night_mode = enabled;
}

bool ui_theme_is_night_mode(void)
{
    return s_night_mode;
}

lv_color_t ui_theme_screen_bg(void)
{
    return s_night_mode ? lv_color_hex(0x000000) : lv_color_hex(0x0A0E1A);
}

void ui_theme_apply(void)
{
    lv_display_t *display = lv_display_get_default();
    if (display == NULL) {
        return;
    }

    lv_obj_t *scr = lv_display_get_screen_active(display);
    if (scr != NULL) {
        lv_obj_set_style_bg_color(scr, ui_theme_screen_bg(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    }

    ui_weather_apply_theme();
    ui_compass_apply_theme();
    ui_wind_strip_apply_theme();
    ui_indoor_apply_theme();
    ui_clock_apply_theme();
    ui_controls_apply_theme();
}
