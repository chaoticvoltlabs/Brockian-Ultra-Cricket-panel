/**
 * @file  hw_init.h
 * @brief Hardware initialisation: I2C bus, backlight, RGB LCD panel.
 */

#ifndef HW_INIT_H
#define HW_INIT_H

#include "esp_lcd_panel_ops.h"
#include "driver/i2c_master.h"

/**
 * @brief  Initialise I2C bus, CH422G backlight, and 800x480 RGB panel.
 * @return Panel handle on success, NULL on failure.
 *
 * The panel is configured with 2 PSRAM framebuffers (for LVGL direct mode).
 * Retrieve them via esp_lcd_rgb_panel_get_frame_buffer().
 */
esp_lcd_panel_handle_t hw_init_panel(void);

/** Return the shared I2C master bus handle (for touch driver, etc.). */
i2c_master_bus_handle_t hw_get_i2c_bus(void);

#endif /* HW_INIT_H */
