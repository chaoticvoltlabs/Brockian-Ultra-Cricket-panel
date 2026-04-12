/**
 * lv_conf.h – LVGL 8.3 configuration
 * Target: Waveshare ESP32-S3-Touch-LCD-7B  (1024 × 600, RGB565)
 */
#if 1   /* Set to 1 to activate */

#ifndef LV_CONF_H
#define LV_CONF_H
#include <stdint.h>

/* ── Colour depth ──────────────────────────────────────────────────────────── */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* ── Memory ────────────────────────────────────────────────────────────────── */
/* Use esp_malloc so PSRAM is available via menuconfig SPIRAM_USE_MALLOC */
#define LV_MEM_CUSTOM 1
#  define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#  define LV_MEM_CUSTOM_ALLOC   malloc
#  define LV_MEM_CUSTOM_FREE    free
#  define LV_MEM_CUSTOM_REALLOC realloc

/* ── HAL ───────────────────────────────────────────────────────────────────── */
#define LV_DISP_DEF_REFR_PERIOD  16   /* ms → ~60 fps */
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_TICK_CUSTOM 1
#  define LV_TICK_CUSTOM_INCLUDE  "esp_timer.h"
#  define LV_TICK_CUSTOM_EXPRESSION ((uint32_t)(esp_timer_get_time() / 1000ULL))

/* ── Logging ───────────────────────────────────────────────────────────────── */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* ── Drawing ───────────────────────────────────────────────────────────────── */
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_USE_FLOAT 1

/* ── GPU (none) ────────────────────────────────────────────────────────────── */
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_SWM341_DMA  0
#define LV_USE_GPU_NXP_PXP     0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL         0

/* ── Fonts ─────────────────────────────────────────────────────────────────── */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

/* ── Widgets ───────────────────────────────────────────────────────────────── */
#define LV_USE_ARC     1
#define LV_USE_BAR     1
#define LV_USE_BTN     1
#define LV_USE_IMG     1
#define LV_USE_LABEL   1
#  define LV_LABEL_TEXT_SELECTION 0
#  define LV_LABEL_LONG_TXT_HINT  0
#define LV_USE_LINE    0
#define LV_USE_METER   1   /* ← gauge widget */

/* ── Themes ────────────────────────────────────────────────────────────────── */
#define LV_USE_THEME_DEFAULT 1
#  define LV_THEME_DEFAULT_DARK 1
#  define LV_THEME_DEFAULT_GROW 0
#  define LV_THEME_DEFAULT_TRANSITION_TIME 80
#define LV_USE_THEME_BASIC 0
#define LV_USE_THEME_MONO  0

/* ── Misc ──────────────────────────────────────────────────────────────────── */
#define LV_USE_USER_DATA 1
#define LV_USE_ASSERT_NULL     1
#define LV_USE_ASSERT_MALLOC   1

#endif /* LV_CONF_H */
#endif /* activation guard */
