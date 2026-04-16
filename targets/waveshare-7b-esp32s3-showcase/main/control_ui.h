#pragma once

#include <stdbool.h>
#include "lvgl.h"

#define CTRL_TARGETS_MAX 6

typedef enum {
    CTRL_OFF,
    CTRL_ON,
    CTRL_UNAVAILABLE,
} ui_ctrl_state_t;

/**
 * Build the showcase control page on the given parent object.
 */
void control_ui_create(lv_obj_t *parent);

/**
 * Show small debug identity text at the bottom-right of the control page.
 */
void control_ui_set_debug_identity(const char *ipv4, const char *mac);

/**
 * Update the climate status line at the top of the control page.
 */
void control_ui_set_room_climate(float temp_c, bool temp_ok, float rh_pct, bool rh_ok);

/**
 * Update one target switch row to the given passive state.
 */
void control_ui_set_target_state(int index, ui_ctrl_state_t state);

/**
 * Look up a target by name (matching panel_profiles.json) and apply state.
 */
void control_ui_set_target_state_by_name(const char *target, ui_ctrl_state_t state);

/**
 * Update the ambient brightness slider and label from live state.
 */
void control_ui_set_ambient_brightness(int pct);

/**
 * Update the ambient color slider and label from live RGB state.
 */
void control_ui_set_ambient_rgb(int r, int g, int b);
