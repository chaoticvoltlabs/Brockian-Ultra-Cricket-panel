#pragma once

/* Copy this file to config.h and fill in your local values before building. */

/* ── WiFi ─────────────────────────────────────────────────────────────────── */
#define WIFI_SSID   "YOUR_WIFI_SSID"
#define WIFI_PASS   "YOUR_WIFI_PASSWORD"

/* ── NTP ──────────────────────────────────────────────────────────────────── */
#define NTP_SERVER  "pool.ntp.org"
/* POSIX timezone string – update for your region if needed */
#define TZ_STRING   "CET-1CEST,M3.5.0,M10.5.0/3"

/* ── Display geometry ─────────────────────────────────────────────────────── */
#define LCD_H_RES   1024
#define LCD_V_RES   600

/* ── RGB panel GPIO (Waveshare ESP32-S3-Touch-LCD-7B) ────────────────────── */
#define LCD_PIN_VSYNC   3
#define LCD_PIN_HSYNC   46
#define LCD_PIN_DE      5
#define LCD_PIN_PCLK    7

#define LCD_PIN_D0      14
#define LCD_PIN_D1      38
#define LCD_PIN_D2      18
#define LCD_PIN_D3      17
#define LCD_PIN_D4      10
#define LCD_PIN_D5      39
#define LCD_PIN_D6      0
#define LCD_PIN_D7      45
#define LCD_PIN_D8      48
#define LCD_PIN_D9      47
#define LCD_PIN_D10     21
#define LCD_PIN_D11     1
#define LCD_PIN_D12     2
#define LCD_PIN_D13     42
#define LCD_PIN_D14     41
#define LCD_PIN_D15     40

#define LCD_PIN_BL      -1

/* ── RGB timing ───────────────────────────────────────────────────────────── */
#define LCD_PCLK_HZ         30850000
#define LCD_HBP             152
#define LCD_HFP             48
#define LCD_HSYNC_W         162
#define LCD_VBP             13
#define LCD_VFP             3
#define LCD_VSYNC_W         45

/* ── GT911 touch (I2C) ────────────────────────────────────────────────────── */
#define TOUCH_I2C_NUM       I2C_NUM_0
#define TOUCH_PIN_SCL       9
#define TOUCH_PIN_SDA       8
#define TOUCH_PIN_RST       -1
#define TOUCH_PIN_INT       4
#define TOUCH_I2C_FREQ_HZ   400000
#define TOUCH_I2C_ADDR      0x5D

/* ── LVGL ─────────────────────────────────────────────────────────────────── */
#define LVGL_BUF_LINES      (LCD_V_RES / 10)
#define LVGL_TASK_STACK     (8 * 1024)
#define LVGL_TASK_PRIO      5

/* ── Backlight ────────────────────────────────────────────────────────────── */
#define BACKLIGHT_DEFAULT_PERCENT 90
