#pragma once

#include <stdbool.h>
#include "lvgl.h"

/**
 * Create a storm alert indicator and register it for shared state updates.
 */
lv_obj_t *panel_alert_create_storm(lv_obj_t *parent, lv_coord_t x, lv_coord_t y);

/**
 * Update every registered storm indicator.
 * Active: slow yellow blink. Inactive: dark gray static triangle.
 */
void panel_alert_set_storm_active(bool active);
