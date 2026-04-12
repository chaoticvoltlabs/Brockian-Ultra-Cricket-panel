/**
 * @file  buc_display.h
 * @brief Hardware constants and color palette for the BUC render-quality test.
 *
 * All pin assignments, LCD timing, and UI geometry live here.
 * Extracted from the proven Waveshare ESP32-S3-LCD-4.3 configuration
 * in local_data/lcd_panel.c and local_data/shared_i2c.c.
 */

#ifndef BUC_DISPLAY_H
#define BUC_DISPLAY_H

#include "lvgl.h"

/* ── Display geometry ───────────────────────────────────────────────── */
#define LCD_H_RES   800
#define LCD_V_RES   480
#define LCD_BPP     2                              /* bytes per pixel (RGB565) */
#define LCD_FB_SIZE (LCD_H_RES * LCD_V_RES * LCD_BPP)

/* ── LCD RGB timing (RK043FN66HS-CTG 800x480) ──────────────────────── */
#define LCD_PCLK_HZ          (16 * 1000 * 1000)   /* 16 MHz pixel clock */
#define LCD_HSYNC_PULSE       4
#define LCD_HSYNC_BACK        8
#define LCD_HSYNC_FRONT       8
#define LCD_VSYNC_PULSE       4
#define LCD_VSYNC_BACK        8
#define LCD_VSYNC_FRONT       8

/* ── GPIO pin assignments ───────────────────────────────────────────── */
#define LCD_PIN_PCLK          7
#define LCD_PIN_VSYNC         3
#define LCD_PIN_HSYNC         46
#define LCD_PIN_DE            5

/* RGB565 data bus: B3-B7, G2-G7, R3-R7 (16 pins) */
#define LCD_PIN_DATA0         14   /* B3 */
#define LCD_PIN_DATA1         38   /* B4 */
#define LCD_PIN_DATA2         18   /* B5 */
#define LCD_PIN_DATA3         17   /* B6 */
#define LCD_PIN_DATA4         10   /* B7 */
#define LCD_PIN_DATA5         39   /* G2 */
#define LCD_PIN_DATA6          0   /* G3 */
#define LCD_PIN_DATA7         45   /* G4 */
#define LCD_PIN_DATA8         48   /* G5 */
#define LCD_PIN_DATA9         47   /* G6 */
#define LCD_PIN_DATA10        21   /* G7 */
#define LCD_PIN_DATA11         1   /* R3 */
#define LCD_PIN_DATA12         2   /* R4 */
#define LCD_PIN_DATA13        42   /* R5 */
#define LCD_PIN_DATA14        41   /* R6 */
#define LCD_PIN_DATA15        40   /* R7 */

/* I2C bus (shared: CH422G + GT911) */
#define I2C_SDA_GPIO          8
#define I2C_SCL_GPIO          9

/* CH422G IO-expander (backlight + GT911 reset) */
#define CH422G_ADDR           0x38
#define CH422G_RST_BIT        (1u << 1)   /* EXIO1 = GT911 RST */
#define CH422G_BL_BIT         (1u << 2)   /* EXIO2 = Backlight  */

/* ── DMA / framebuffer config ───────────────────────────────────────── */
#define LCD_NUM_FB            2                    /* double-FB for LVGL direct mode */
#define LCD_BOUNCE_BUF_PX     (LCD_H_RES * 10)    /* DRAM bounce for PSRAM DMA */
#define LCD_DMA_BURST_SZ      64                   /* bytes */

/* ── LVGL task config ───────────────────────────────────────────────── */
#define LVGL_TICK_PERIOD_MS   2
#define LVGL_TASK_STACK       (6 * 1024)
#define LVGL_TASK_PRIO        2

/* ── Color palette (dark premium theme) ─────────────────────────────── */
#define COL_BG            lv_color_hex(0x0A0E1A)   /* screen background      */
#define COL_CARD_BG       lv_color_hex(0x111622)   /* weather card bg         */
#define COL_WHITE         lv_color_hex(0xFFFFFF)   /* hero temperature        */
#define COL_LIGHT_GREY    lv_color_hex(0xB0B8C8)   /* subtitle text           */
#define COL_MED_GREY      lv_color_hex(0x7880A0)   /* summary text            */
#define COL_LABEL_GREY    lv_color_hex(0x9098B0)   /* summary labels          */
#define COL_SEPARATOR     lv_color_hex(0x222838)   /* divider lines           */
#define COL_RING          lv_color_hex(0x2A3040)   /* compass outer ring      */
#define COL_TICK_MAJOR    lv_color_hex(0xC0C8D8)   /* compass major ticks     */
#define COL_TICK_MINOR    lv_color_hex(0x505868)   /* compass minor ticks     */
#define COL_CARDINAL      lv_color_hex(0xD0D8E8)   /* compass N/O/Z/W labels  */
#define COL_ACCENT_BLUE   lv_color_hex(0x4488FF)   /* needle, wind fill       */
#define COL_ACCENT_ORANGE lv_color_hex(0xFF8844)   /* gust fill               */
#define COL_STRIP_BG      lv_color_hex(0x1A2030)   /* wind strip track        */

#endif /* BUC_DISPLAY_H */
