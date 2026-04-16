#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Left-column outdoor weather block (port of ui_weather.c page-0 variant
 * from the 4.3" target). Layout, top to bottom:
 *     hero temperature   (Montserrat 48, white)
 *     "Feels X°C"        (Montserrat 20, muted)
 *     XX% RH             (Montserrat 20, muted)
 *     ---
 *     Wind label + Bft value (Montserrat 36, gradient) + km/h caption
 *     Vlagen label + Bft value
 *     ---
 *     clock HH:MM        (Montserrat 28, secondary)
 *     ---
 *     pressure hPa + 24h trend line graph (auto-scaled, min 6 hPa range)
 */

void weather_col_create(lv_obj_t *parent);

void weather_col_set_outdoor(float temp_c, bool temp_ok,
                             float feel_c, bool feel_ok,
                             int rh_pct, bool rh_ok);

void weather_col_set_wind(int wind_bft, int gust_bft, int wind_kmh);

void weather_col_set_pressure(int hpa, bool hpa_ok,
                              const float *trend, int n);

/* Call once per second from the LVGL task; refreshes the HH:MM label. */
void weather_col_tick(void);

#ifdef __cplusplus
}
#endif
