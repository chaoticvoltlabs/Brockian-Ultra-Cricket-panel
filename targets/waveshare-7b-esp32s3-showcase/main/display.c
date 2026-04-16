/**
 * display.c
 *
 * Initialises the Waveshare 7" RGB 1024×600 panel via esp_lcd_panel_rgb and
 * registers an LVGL display driver with double-buffered full refresh.
 *
 * Strategy: two PSRAM framebuffers + bounce buffer.
 *
 *   • num_fbs=2  – one framebuffer is scanned out while LVGL renders into the
 *     other, avoiding visible in-place redraw artefacts.
 *   • bounce_buffer_size_px = LCD_H_RES*20  – DMA reads via SRAM bounce,
 *     not directly from PSRAM, which is required for reliable operation on
 *     ESP32-S3.  Larger bounce window absorbs brief PSRAM-bus contention
 *     (e.g. during the 10s weather fetch) without scanline tearing.
 *   • full_refresh=1  – LVGL renders a complete frame into the back buffer.
 *   • flush_cb  – pushes the rendered frame to PSRAM, asks the RGB driver to
 *     switch to that framebuffer, then waits for VSYNC before returning.
 *
 * This trades some throughput for clean animation without the rectangular
 * redraw windows seen with single-framebuffer direct rendering.
 */

#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_cache.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "lvgl.h"

#include "config.h"
#include "display.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel   = NULL;
static lv_disp_draw_buf_t     s_draw_buf;
static TaskHandle_t           s_flush_task = NULL;
static volatile uint32_t      s_vsync_count = 0;

static bool IRAM_ATTR rgb_vsync_cb(esp_lcd_panel_handle_t panel,
                                   const esp_lcd_rgb_panel_event_data_t *edata,
                                   void *user_ctx)
{
    LV_UNUSED(panel);
    LV_UNUSED(edata);
    LV_UNUSED(user_ctx);

    s_vsync_count++;

    BaseType_t high_task_wakeup = pdFALSE;
    if (s_flush_task != NULL) {
        vTaskNotifyGiveFromISR(s_flush_task, &high_task_wakeup);
    }
    return high_task_wakeup == pdTRUE;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * LVGL flush callback  –  swap framebuffer on VSYNC
 * ───────────────────────────────────────────────────────────────────────────── */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_map)
{
    LV_UNUSED(area);

    if (s_flush_task == NULL) {
        s_flush_task = xTaskGetCurrentTaskHandle();
    }

    size_t fb_bytes = (size_t)LCD_H_RES * LCD_V_RES * sizeof(lv_color_t);
    esp_cache_msync(color_map, fb_bytes,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, color_map));

    uint32_t pre = s_vsync_count;
    ulTaskNotifyValueClear(NULL, ULONG_MAX);
    while (s_vsync_count == pre) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    lv_disp_flush_ready(drv);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────────── */
void display_init(lv_disp_t **out_disp)
{
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src        = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz            = LCD_PCLK_HZ,
            .h_res              = LCD_H_RES,
            .v_res              = LCD_V_RES,
            .hsync_back_porch   = LCD_HBP,
            .hsync_front_porch  = LCD_HFP,
            .hsync_pulse_width  = LCD_HSYNC_W,
            .vsync_back_porch   = LCD_VBP,
            .vsync_front_porch  = LCD_VFP,
            .vsync_pulse_width  = LCD_VSYNC_W,
            .flags.pclk_active_neg = 1,
        },
        .data_width     = 16,
        .num_fbs        = 2,                        /* double framebuffer    */
        .bounce_buffer_size_px = LCD_H_RES * 20,    /* SRAM bounce for PSRAM DMA */
        .hsync_gpio_num  = LCD_PIN_HSYNC,
        .vsync_gpio_num  = LCD_PIN_VSYNC,
        .de_gpio_num     = LCD_PIN_DE,
        .pclk_gpio_num   = LCD_PIN_PCLK,
        .disp_gpio_num   = -1,
        .data_gpio_nums  = {
            LCD_PIN_D0,  LCD_PIN_D1,  LCD_PIN_D2,  LCD_PIN_D3,
            LCD_PIN_D4,  LCD_PIN_D5,  LCD_PIN_D6,  LCD_PIN_D7,
            LCD_PIN_D8,  LCD_PIN_D9,  LCD_PIN_D10, LCD_PIN_D11,
            LCD_PIN_D12, LCD_PIN_D13, LCD_PIN_D14, LCD_PIN_D15,
        },
        .flags.fb_in_psram = 1,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = rgb_vsync_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, NULL));

    if (LCD_PIN_BL >= 0) {
        gpio_set_direction((gpio_num_t)LCD_PIN_BL, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)LCD_PIN_BL, 1);
    }

    ESP_LOGI(TAG, "RGB panel initialised  %d×%d @ %ld Hz",
             LCD_H_RES, LCD_V_RES, (long)LCD_PCLK_HZ);

    void *fb1 = NULL;
    void *fb2 = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &fb1, &fb2));
    size_t fb_size = (size_t)LCD_H_RES * LCD_V_RES;   /* pixels */

    lv_disp_draw_buf_init(&s_draw_buf, fb1, fb2, fb_size);

    ESP_LOGI(TAG, "Frame buffers: %p %p  (%u bytes each)", fb1, fb2,
             (unsigned)(fb_size * sizeof(lv_color_t)));

    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res      = LCD_H_RES;
    drv.ver_res      = LCD_V_RES;
    drv.flush_cb     = lvgl_flush_cb;
    drv.draw_buf     = &s_draw_buf;
    drv.full_refresh = 1;               /* render a full frame into backbuffer */
    drv.direct_mode  = 0;

    lv_disp_t *disp = lv_disp_drv_register(&drv);
    if (out_disp) *out_disp = disp;

    ESP_LOGI(TAG, "LVGL display driver registered (double-buf, full refresh)");
}
