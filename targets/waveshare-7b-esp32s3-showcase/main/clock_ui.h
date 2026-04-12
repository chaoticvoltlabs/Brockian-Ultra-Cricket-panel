#pragma once

/**
 * Build the ECharts-style gauge clock on the given parent object.
 * The parent is expected to fill the screen.
 */
void clock_ui_create(lv_obj_t *parent);

/**
 * Update the left-side room context block.
 */
void clock_ui_set_room_context(const char *room_name, float temp_c, int rh_pct);

/**
 * Update all gauge needles / labels to the current local time.
 * Call every second from the LVGL task.
 */
void clock_ui_tick(void);
