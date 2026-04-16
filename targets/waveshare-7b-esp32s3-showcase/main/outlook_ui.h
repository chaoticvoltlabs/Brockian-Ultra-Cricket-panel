#pragma once

#include "lvgl.h"

/**
 * Build a calm 48h weather outlook page on the given parent object.
 */
void outlook_ui_create(lv_obj_t *parent);

/**
 * Update the "Now" section with live observations from BUC-server.
 * wind_bft/gust_bft are clamped to [0,12]; gust is never shown below wind.
 * dir_deg is normalised into [0,360).
 */
void outlook_ui_set_current(int wind_bft, int gust_bft, int dir_deg);

/**
 * Update the 48h forecast chart + peak-gust label.
 * wind_bft/gust_bft arrays are 8 samples at offsets {0,6,12,18,24,30,36,42}h.
 * Values are clamped to [0,12]. peak_hour_offset is shown as "+Nh".
 */
void outlook_ui_set_forecast(const int wind_bft[8], const int gust_bft[8],
                             int peak_gust_bft, int peak_hour_offset);
