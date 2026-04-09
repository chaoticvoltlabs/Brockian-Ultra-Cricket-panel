/**
 * @file  hw_touch.h
 * @brief GT911 capacitive touch driver for LVGL.
 */

#ifndef HW_TOUCH_H
#define HW_TOUCH_H

#include "driver/i2c_master.h"
#include "lvgl.h"

/**
 * Initialise GT911 on the shared I2C bus and register an LVGL pointer
 * input device. The CH422G reset sequence must have completed first
 * (selects GT911 address 0x5D). Call once after lv_init + display create.
 */
void hw_touch_init(i2c_master_bus_handle_t bus, lv_display_t *display);

#endif /* HW_TOUCH_H */
