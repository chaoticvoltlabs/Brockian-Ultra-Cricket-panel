/**
 * @file  ui_wind_strip.h
 * @brief Horizontal wind/gust bar on a Beaufort scale (0-12).
 */

#ifndef UI_WIND_STRIP_H
#define UI_WIND_STRIP_H

#include "lvgl.h"
#include <stdbool.h>

/** Create the wind strip as a child of @p parent. Call once. */
void ui_wind_strip_create(lv_obj_t *parent);

/** Update the bar to show current wind and gust in Beaufort. */
void ui_wind_strip_update(float wind_bft, float gust_bft);

/**
 * Set the marker "transport" state. Currently wired to WiFi:
 *   true  -> white marker (connectivity ok)
 *   false -> red marker   (no WiFi / no DHCP)
 * The marker semantically represents the connectivity layer.
 */
void ui_wind_strip_set_connected(bool connected);

#endif /* UI_WIND_STRIP_H */
