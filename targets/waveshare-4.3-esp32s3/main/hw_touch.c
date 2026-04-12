/**
 * @file  hw_touch.c
 * @brief Thin GT911 touch driver — polls register 0x814E over I2C.
 *
 * The CH422G reset sequence in hw_init.c already asserted the GT911 reset
 * with INT=low, selecting I2C address 0x5D. This driver reads up to one
 * touch point per LVGL indev poll and feeds it as a pointer device.
 *
 * GT911 register map (only what we need):
 *   0x814E  [1 byte]  status: bit 7 = buffer ready, bits 3:0 = touch count
 *   0x8150  [6 bytes per point]  x_lo, x_hi, y_lo, y_hi, size_lo, size_hi
 * After reading, write 0x00 to 0x814E to acknowledge.
 */

#include "hw_touch.h"
#include "buc_display.h"

#include "esp_log.h"
#include <string.h>

static const char *TAG = "touch";

#define GT911_ADDR          0x5D
#define GT911_REG_STATUS    0x814E
#define GT911_REG_POINT1    0x8150

static i2c_master_dev_handle_t s_dev;

/* ── Low-level I2C helpers ─────────────────────────────────────────── */

static esp_err_t gt_read_reg(uint16_t reg, uint8_t *buf, size_t len)
{
    uint8_t cmd[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(s_dev, cmd, 2, buf, len, 50);
}

static esp_err_t gt_write_reg(uint16_t reg, uint8_t val)
{
    uint8_t cmd[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val };
    return i2c_master_transmit(s_dev, cmd, 3, 50);
}

/* ── LVGL indev read callback ──────────────────────────────────────── */

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->continue_reading = false;

    uint8_t status = 0;
    if (gt_read_reg(GT911_REG_STATUS, &status, 1) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    bool buf_ready = (status & 0x80) != 0;
    int  count     = status & 0x0F;

    if (!buf_ready || count == 0) {
        if (buf_ready) gt_write_reg(GT911_REG_STATUS, 0x00);
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    /* Read first touch point (6 bytes) */
    uint8_t pt[6];
    if (gt_read_reg(GT911_REG_POINT1, pt, 6) != ESP_OK) {
        gt_write_reg(GT911_REG_STATUS, 0x00);
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    /* Acknowledge */
    gt_write_reg(GT911_REG_STATUS, 0x00);

    uint16_t raw_x = (uint16_t)pt[0] | ((uint16_t)pt[1] << 8);
    uint16_t raw_y = (uint16_t)pt[2] | ((uint16_t)pt[3] << 8);

    /* Waveshare 4.3" landscape: GT911 native coords match screen. */
    data->point.x = (int32_t)raw_x;
    data->point.y = (int32_t)raw_y;
    data->state   = LV_INDEV_STATE_PRESSED;
}

/* ── Public API ────────────────────────────────────────────────────── */

void hw_touch_init(i2c_master_bus_handle_t bus, lv_display_t *display)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = GT911_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev));

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, display);

    ESP_LOGI(TAG, "GT911 indev registered (addr=0x%02X)", GT911_ADDR);
}
