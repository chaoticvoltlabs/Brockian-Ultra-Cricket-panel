#pragma once

#include "lvgl.h"

/**
 * Initialise the RGB LCD panel and register it as the LVGL display driver.
 * Must be called once before any lv_* draw calls.
 *
 * @param[out] disp  Pointer to the created lv_disp_t (may be NULL if caller
 *                   doesn't need it).
 */
void display_init(lv_disp_t **disp);
