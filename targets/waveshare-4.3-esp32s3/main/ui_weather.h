/**
 * @file  ui_weather.h
 * @brief Weather hero block: large temperature, feels-like, summary lines.
 */

#ifndef UI_WEATHER_H
#define UI_WEATHER_H

#include "lvgl.h"
#include "demo_data.h"

/** Create the weather block as a child of @p parent. Call once. */
void ui_weather_create(lv_obj_t *parent);

/** Update all labels with a fresh weather sample. */
void ui_weather_update(const demo_data_t *d);

/**
 * Set the room climate data (shown as hero temp + secondary RH
 * on page 3 instead of outdoor temp / "Feels Like").
 */
void ui_weather_set_room_climate(float temp_c, bool temp_valid,
                                  float rh_pct, bool rh_valid);

/**
 * Notify the weather column which page is currently visible.
 * Pages 0-1: secondary line shows "Feels Like X°C".
 * Page 2:    secondary line shows local room RH%.
 */
void ui_weather_set_page(int page);

/**
 * Overwrite the barometric trend with an array of hPa values (oldest -> newest).
 * Typically called from panel_api when the upstream trend array arrives.
 * @p count is clamped to the internal history size. Safe to call from
 * within the LVGL lock.
 */
void ui_weather_set_baro_trend(const float *values, int count);

/** Re-apply palette-dependent styles after a theme switch. */
void ui_weather_apply_theme(void);

/** Show or hide the global storm warning indicator. */
void ui_weather_set_storm_active(bool active);

#endif /* UI_WEATHER_H */
