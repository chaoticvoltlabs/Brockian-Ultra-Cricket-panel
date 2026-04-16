#pragma once

#include "lvgl.h"

/* Build the analog clock inside the given parent container. */
void ui_clock_create(lv_obj_t *parent);

/* Update the clock to the current local time. Call roughly once per second. */
void ui_clock_tick(void);

/* Re-apply palette-dependent styles after a theme switch. */
void ui_clock_apply_theme(void);
