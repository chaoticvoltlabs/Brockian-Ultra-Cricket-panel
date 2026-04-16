/**
 * @file  ui_indoor.h
 * @brief Indoor climate matrix: 3 rows × 4 zones, temp + RH per cell.
 */

#ifndef UI_INDOOR_H
#define UI_INDOOR_H

#include "lvgl.h"
#include <stdbool.h>

#define INDOOR_ZONES  12

/** Create the indoor matrix as a child of @p parent. Call once. */
void ui_indoor_create(lv_obj_t *parent);

/**
 * Update a single zone.
 * @param index       0-11 (row-major: boven 0-3, begane grond 4-7, kelder 8-11)
 * @param temp_c      temperature in °C  (ignored when temp_valid is false)
 * @param rh_pct      relative humidity % (ignored when rh_valid is false)
 * @param temp_valid  false → show "--" placeholder for temperature
 * @param rh_valid    false → show "--%" placeholder for humidity
 */
void ui_indoor_update(int index, float temp_c, float rh_pct,
                      bool temp_valid, bool rh_valid);

/** Re-apply palette-dependent styles after a theme switch. */
void ui_indoor_apply_theme(void);

#endif /* UI_INDOOR_H */
