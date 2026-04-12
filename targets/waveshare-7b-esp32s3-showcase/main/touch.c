/**
 * touch.c
 *
 * GT911 capacitive touch driver via espressif/esp_lcd_touch_gt911 managed
 * component.  Registers an LVGL input device.
 */

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch_gt911.h"
#include "lvgl.h"

#include "config.h"
#include "touch.h"

static const char *TAG = "touch";

static esp_lcd_touch_handle_t s_touch = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_expander = NULL;

#define IOX_ADDR             0x24
#define IOX_REG_MODE         0x02
#define IOX_REG_OUTPUT       0x03
#define IOX_REG_PWM          0x05
#define IOX_PIN_BACKLIGHT    2

static uint8_t s_expander_out = 0xFF;

static esp_err_t expander_write_reg(uint8_t reg, uint8_t value)
{
    if (s_expander == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t payload[2] = { reg, value };
    return i2c_master_transmit(s_expander, payload, sizeof(payload), pdMS_TO_TICKS(50));
}

void backlight_set_percent(uint8_t percent)
{
    if (s_expander == NULL) {
        ESP_LOGW(TAG, "Backlight control not ready");
        return;
    }

    if (percent > 100) {
        percent = 100;
    }

    if (percent == 0) {
        s_expander_out &= (uint8_t)~(1U << IOX_PIN_BACKLIGHT);
        expander_write_reg(IOX_REG_OUTPUT, s_expander_out);
        ESP_LOGI(TAG, "Backlight off");
        return;
    }

    s_expander_out |= (uint8_t)(1U << IOX_PIN_BACKLIGHT);
    expander_write_reg(IOX_REG_OUTPUT, s_expander_out);

    /* CH422G PWM is effectively inverted for the backlight path: 0 = brightest. */
    uint8_t pwm_percent = (uint8_t)(100 - percent);
    if (pwm_percent >= 97) {
        pwm_percent = 97;
    }
    uint8_t pwm_raw = (uint8_t)(pwm_percent * 255 / 100);
    expander_write_reg(IOX_REG_PWM, pwm_raw);

    ESP_LOGI(TAG, "Backlight %u%% (PWM=%u)", percent, pwm_raw);
}

/* ── LVGL input-device read callback ────────────────────────────────────────── */
static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t x[1], y[1];
    uint16_t strength[1];
    uint8_t  point_num = 0;

    esp_lcd_touch_read_data(s_touch);
    bool touched = esp_lcd_touch_get_coordinates(s_touch, x, y, strength,
                                                 &point_num, 1);
    if (touched && point_num > 0) {
        data->point.x = x[0];
        data->point.y = y[0];
        data->state   = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

/* ── Public API ──────────────────────────────────────────────────────────────── */
void touch_init(void)
{
    esp_err_t ret;

    /* ── I2C master bus ──────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "i2c_new_master_bus SDA=%d SCL=%d port=%d",
             TOUCH_PIN_SDA, TOUCH_PIN_SCL, TOUCH_I2C_NUM);

    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port             = TOUCH_I2C_NUM,
        .sda_io_num           = TOUCH_PIN_SDA,
        .scl_io_num           = TOUCH_PIN_SCL,
        .clk_source           = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt    = 7,
        .flags.enable_internal_pullup = true,
    };
    ret = i2c_new_master_bus(&i2c_bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s – touch disabled", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "I2C master bus created");

    /* ── IO expander at 0x24 (TCA9534/PCA9534-compatible) ───────────────────
     * Controls DISP_EN, CTP_RST (EXIO1), backlight and other board signals.
     * Must be initialised before GT911 and before display shows anything.   */
    {
        i2c_device_config_t exp_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = IOX_ADDR,
            .scl_speed_hz    = TOUCH_I2C_FREQ_HZ,
        };
        if (i2c_master_bus_add_device(s_i2c_bus, &exp_cfg, &s_expander) == ESP_OK) {
            s_expander_out = 0xFF;
            expander_write_reg(IOX_REG_MODE, 0xFF);
            expander_write_reg(IOX_REG_OUTPUT, s_expander_out);
            ESP_LOGI(TAG, "IO expander 0x24: all outputs enabled");
            backlight_set_percent(100);
        } else {
            ESP_LOGW(TAG, "IO expander 0x24: could not add device");
        }
    }

    /* ── GT911 panel IO ───────────────────────────────────────────────────── */
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.dev_addr    = TOUCH_I2C_ADDR;
    tp_io_cfg.scl_speed_hz = TOUCH_I2C_FREQ_HZ;  /* required by new I2C master driver */

    ESP_LOGI(TAG, "esp_lcd_new_panel_io_i2c addr=0x%02x", TOUCH_I2C_ADDR);
    ret = esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_i2c failed: %s – touch disabled", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Panel IO created");

    /* ── GT911 driver ─────────────────────────────────────────────────────── */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_H_RES,
        .y_max        = LCD_V_RES,
        .rst_gpio_num = TOUCH_PIN_RST,   /* -1 = not wired */
        .int_gpio_num = TOUCH_PIN_INT,   /* -1 = not wired */
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_LOGI(TAG, "esp_lcd_touch_new_i2c_gt911 RST=%d INT=%d",
             TOUCH_PIN_RST, TOUCH_PIN_INT);
    ret = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_touch_new_i2c_gt911 failed: %s – touch disabled", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "GT911 touch initialised");

    /* ── Register LVGL input device ──────────────────────────────────────── */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "LVGL touch input device registered");
}
