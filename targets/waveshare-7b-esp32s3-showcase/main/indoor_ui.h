#pragma once

#include <stdbool.h>
#include "lvgl.h"

#define INDOOR_ZONES 12

/**
 * Build the indoor-climate matrix (3 floor rows × 4 zone cells) on parent.
 */
void indoor_ui_create(lv_obj_t *parent);

/**
 * Update one zone cell. Index 0..11, row-major:
 *   boven 0-3, begane grond 4-7, kelder 8-11.
 * temp_ok=false → "--" placeholder; rh_ok=false → "--%".
 */
void indoor_ui_set_zone(int index, float temp_c, float rh_pct,
                        bool temp_ok, bool rh_ok);
