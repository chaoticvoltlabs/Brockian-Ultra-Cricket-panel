#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdbool.h>
#include "lvgl.h"

void ui_theme_set_night_mode(bool enabled);
bool ui_theme_is_night_mode(void);
lv_color_t ui_theme_screen_bg(void);
void ui_theme_apply(void);

#endif /* UI_THEME_H */
