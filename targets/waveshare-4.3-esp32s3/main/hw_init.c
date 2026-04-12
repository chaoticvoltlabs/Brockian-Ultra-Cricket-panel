/**
 * @file  hw_init.c
 * @brief Hardware initialisation for Waveshare ESP32-S3-LCD-4.3.
 *
 * Merges the I2C bus setup (from local_data/shared_i2c.c) and the LCD panel
 * + CH422G backlight init (from local_data/lcd_panel.c) into a single file.
 * Touch (GT911) uses the same I2C bus -- see hw_touch.c.
 */

#include "hw_init.h"
#include "buc_display.h"

#include "driver/i2c_master.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "hw_init";

/* ── I2C master bus (singleton) ─────────────────────────────────────── */

static i2c_master_bus_handle_t s_i2c_bus = NULL;

static i2c_master_bus_handle_t i2c_init(void)
{
    if (s_i2c_bus) return s_i2c_bus;

    i2c_master_bus_config_t cfg = {
        .i2c_port                     = I2C_NUM_0,
        .sda_io_num                   = I2C_SDA_GPIO,
        .scl_io_num                   = I2C_SCL_GPIO,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_i2c_bus));
    ESP_LOGI(TAG, "I2C bus created (SDA=%d, SCL=%d)", I2C_SDA_GPIO, I2C_SCL_GPIO);
    return s_i2c_bus;
}

/* ── CH422G backlight + GT911 reset sequence ────────────────────────── */
/*
 * CH422G write register is write-only; one byte sets ALL EXIO outputs.
 * Sequence selects GT911 I2C address 0x5D (INT low during reset):
 *   1. EXIO1=0, EXIO2=0  → RST assert, backlight off
 *   2. Wait 10 ms
 *   3. EXIO1=1, EXIO2=0  → RST release
 *   4. Wait 50 ms         → GT911 boot
 *   5. EXIO1=1, EXIO2=1  → backlight on
 */
static void backlight_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CH422G_ADDR,
        .scl_speed_hz    = 400000,
    };
    i2c_master_dev_handle_t dev;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &dev));

    uint8_t val;

    val = 0x00;                                    /* RST assert, BL off */
    ESP_ERROR_CHECK(i2c_master_transmit(dev, &val, 1, 100));
    vTaskDelay(pdMS_TO_TICKS(10));

    val = CH422G_RST_BIT;                          /* RST release, BL off */
    ESP_ERROR_CHECK(i2c_master_transmit(dev, &val, 1, 100));
    vTaskDelay(pdMS_TO_TICKS(50));

    val = CH422G_RST_BIT | CH422G_BL_BIT;         /* RST released + BL on */
    ESP_ERROR_CHECK(i2c_master_transmit(dev, &val, 1, 100));
    ESP_LOGI(TAG, "CH422G: backlight on, GT911 RST released");
}

/* ── RGB LCD panel creation ─────────────────────────────────────────── */

esp_lcd_panel_handle_t hw_init_panel(void)
{
    i2c_master_bus_handle_t bus = i2c_init();
    backlight_init(bus);

    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src            = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz            = LCD_PCLK_HZ,
            .h_res              = LCD_H_RES,
            .v_res              = LCD_V_RES,
            .hsync_pulse_width  = LCD_HSYNC_PULSE,
            .hsync_back_porch   = LCD_HSYNC_BACK,
            .hsync_front_porch  = LCD_HSYNC_FRONT,
            .vsync_pulse_width  = LCD_VSYNC_PULSE,
            .vsync_back_porch   = LCD_VSYNC_BACK,
            .vsync_front_porch  = LCD_VSYNC_FRONT,
            .flags.pclk_active_neg = true,
        },
        .data_width         = 16,                  /* RGB565 */
        .num_fbs            = LCD_NUM_FB,          /* 2 for LVGL direct mode */
        .bounce_buffer_size_px = LCD_BOUNCE_BUF_PX,
        .dma_burst_size     = LCD_DMA_BURST_SZ,
        .hsync_gpio_num     = LCD_PIN_HSYNC,
        .vsync_gpio_num     = LCD_PIN_VSYNC,
        .de_gpio_num        = LCD_PIN_DE,
        .pclk_gpio_num      = LCD_PIN_PCLK,
        .disp_gpio_num      = -1,                  /* not used */
        .data_gpio_nums = {
            LCD_PIN_DATA0,  LCD_PIN_DATA1,  LCD_PIN_DATA2,  LCD_PIN_DATA3,
            LCD_PIN_DATA4,  LCD_PIN_DATA5,  LCD_PIN_DATA6,  LCD_PIN_DATA7,
            LCD_PIN_DATA8,  LCD_PIN_DATA9,  LCD_PIN_DATA10, LCD_PIN_DATA11,
            LCD_PIN_DATA12, LCD_PIN_DATA13, LCD_PIN_DATA14, LCD_PIN_DATA15,
        },
        .flags.fb_in_psram  = true,
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    ESP_LOGI(TAG, "RGB panel init OK  %dx%d  %d FBs", LCD_H_RES, LCD_V_RES, LCD_NUM_FB);
    return panel;
}

i2c_master_bus_handle_t hw_get_i2c_bus(void)
{
    return s_i2c_bus;
}
