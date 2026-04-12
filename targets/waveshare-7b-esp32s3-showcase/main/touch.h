#pragma once

#include <stdint.h>

/** Initialise the GT911 touch controller and register it as an LVGL input device. */
void touch_init(void);

/** Set LCD backlight brightness in percent (0 = off, 100 = brightest). */
void backlight_set_percent(uint8_t percent);
