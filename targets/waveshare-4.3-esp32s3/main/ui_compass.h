/**
 * @file  ui_compass.h
 * @brief Wind compass widget: ring, ticks, cardinal labels, needle.
 */

#ifndef UI_COMPASS_H
#define UI_COMPASS_H

#include "lvgl.h"
#include <stdbool.h>

/** Create the compass as a child of @p parent. Call once. */
void ui_compass_create(lv_obj_t *parent);

/** Update the needle direction (0 = North, clockwise, degrees). */
void ui_compass_set_direction(float deg);

/**
 * Set the needle "content validity" state. Currently wired to WiFi:
 *   true  -> normal amber/gold needle
 *   false -> red needle (no WiFi / no data transport)
 * Semantically this is the "valid content" indicator and will later
 * also reflect live-data freshness on top of transport.
 */
void ui_compass_set_connected(bool connected);

#endif /* UI_COMPASS_H */
