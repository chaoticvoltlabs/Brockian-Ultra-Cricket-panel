/**
 * @file  main.c
 * @brief BUC render-quality test – LVGL on Waveshare ESP32-S3-LCD-4.3.
 *
 * Initialises the 800x480 RGB panel, sets up LVGL with double-framebuffer
 * direct mode, creates a dark-themed weather dashboard, and starts a demo
 * data timer that drifts values every few seconds.
 *
 * WiFi: hardcoded STA credentials via main/secrets.h, basic reconnect.
 * Status is polled by an LVGL timer that drives the compass needle and
 * wind strip marker colours (red = no connectivity / no valid content).
 *
 * LVGL integration follows the official ESP-IDF v6 RGB panel example:
 *   esp-idf/examples/peripherals/lcd/rgb_panel/main/rgb_lcd_example_main.c
 */

#include <unistd.h>    /* usleep */
#include <stdlib.h>    /* setenv */
#include <time.h>      /* tzset  */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "nvs_flash.h"
#include "lvgl.h"

#include "buc_display.h"
#include "build_number.h"
#include "hw_init.h"
#include "hw_touch.h"
#include "ui_weather.h"
#include "ui_compass.h"
#include "ui_wind_strip.h"
#include "ui_pages.h"
#include "ui_indoor.h"
#include "ui_controls.h"
#include "net_wifi.h"
#include "panel_api.h"
#include "app_lvgl_lock.h"
#include "esp_netif_sntp.h"

static const char *TAG = "buc";

static void page3_scene_press_cb(int index, ui_ctrl_press_kind_t kind)
{
    if (kind == UI_CTRL_PRESS_LONG) {
        panel_api_send_scene_command(index, 1);
        ui_controls_set_active_scene(-1);
        return;
    }

    panel_api_send_scene_command(index, 0);
    ui_controls_set_active_scene(index);
}

static void page3_target_press_cb(int index)
{
    panel_api_send_target_command(index);
}

/* ── LVGL thread-safety lock ────────────────────────────────────────── */
/* Non-static so the HTTP client task (panel_api.c) can acquire it via
 * app_lvgl_lock.h when pushing fresh data into the UI. */
_lock_t lvgl_api_lock;

/* ── LVGL flush callback ────────────────────────────────────────────── */
/*
 * Flush-pending guard: on_vsync fires every frame (~60 Hz), but
 * lv_display_flush_ready must only be called when LVGL actually
 * submitted a new buffer via flush_cb.  A simple volatile flag
 * bridges the LVGL task and the ISR safely (single writer each side,
 * single bool, no torn reads on Xtensa).
 */
static volatile bool s_flush_pending = false;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    s_flush_pending = true;
    esp_lcd_panel_draw_bitmap(panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
}

/* ── VSYNC callback (runs in ISR context) ──────────────────────────── */
/*
 * Uses on_vsync instead of on_color_trans_done.  on_color_trans_done
 * fires when the DMA bounce-buffer copy finishes, which is BEFORE the
 * frame has been fully scanned out — so LVGL could start writing the
 * next frame into a buffer the LCD is still reading, causing tearing.
 *
 * on_vsync fires at the actual vertical-sync pulse, when the LCD has
 * finished the current frame scan.  At that point the old buffer is
 * truly free and LVGL can safely render into it.
 */
static bool IRAM_ATTR notify_flush_ready(
        esp_lcd_panel_handle_t panel,
        const esp_lcd_rgb_panel_event_data_t *edata,
        void *user_ctx)
{
    if (s_flush_pending) {
        s_flush_pending = false;
        lv_display_t *disp = (lv_display_t *)user_ctx;
        lv_display_flush_ready(disp);
    }
    return false;
}

/* ── LVGL tick (2 ms periodic timer) ────────────────────────────────── */
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/* ── WiFi status poll (LVGL timer, runs inside lv_timer_handler) ───── */
/*
 * Polls net_wifi_get_status() and propagates it to both UI indicators.
 * Runs under the LVGL lock via lv_timer_handler, so it can safely call
 * UI APIs without extra locking. Keeps last value to avoid needless
 * invalidations when nothing changed.
 */
static void wifi_status_timer_cb(lv_timer_t *t)
{
    (void)t;
    static int last = -1;   /* -1 = first call, forces push on first tick */
    int now = (int)net_wifi_get_status();
    if (now == last) return;
    last = now;

    bool connected = (now == NET_WIFI_CONNECTED);
    ui_compass_set_connected(connected);
    ui_wind_strip_set_connected(connected);
}

/* ── LVGL task loop ─────────────────────────────────────────────────── */
#define LVGL_MIN_DELAY_MS  (1000 / CONFIG_FREERTOS_HZ)  /* avoid WDT */
#define LVGL_MAX_DELAY_MS  500

static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task started");

    for (;;) {
        _lock_acquire(&lvgl_api_lock);
        uint32_t delay_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);

        if (delay_ms < LVGL_MIN_DELAY_MS) delay_ms = LVGL_MIN_DELAY_MS;
        if (delay_ms > LVGL_MAX_DELAY_MS) delay_ms = LVGL_MAX_DELAY_MS;
        usleep(delay_ms * 1000);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== BUC Render Quality Test ===");
    ESP_LOGI(TAG, "Build #%d  (%s %s)", BUILD_NUMBER, __DATE__, __TIME__);

    /* 0. NVS (required by WiFi) */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    /* 1. Hardware: I2C + backlight + RGB panel */
    esp_lcd_panel_handle_t panel = hw_init_panel();
    if (!panel) {
        ESP_LOGE(TAG, "Panel init failed");
        return;
    }

    /* 2. LVGL core */
    lv_init();

    /* 3. Create LVGL display */
    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_user_data(display, panel);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

    /* 4. Double-framebuffer full mode.
     *    FULL renders the entire screen into the off-screen buffer, then
     *    swaps at VSYNC — no partial writes to the displayed buffer, so
     *    no visible tearing or mid-frame artefacts.  The trade-off (full
     *    redraw instead of dirty-area patches) is negligible for a mostly
     *    static dashboard that updates once every ~10 s. */
    void *buf1 = NULL, *buf2 = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &buf1, &buf2));
    lv_display_set_buffers(display, buf1, buf2,
                           LCD_FB_SIZE, LV_DISPLAY_RENDER_MODE_FULL);

    /* 5. Flush callback */
    lv_display_set_flush_cb(display, flush_cb);

    /* 6. Register VSYNC event → signals buffer recycling to LVGL */
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = notify_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, display));

    /* 7. Tick timer (2 ms) */
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    /* 8. Touch input (GT911, same I2C bus, addr 0x5D) */
    hw_touch_init(hw_get_i2c_bus(), display);

    /* 9. Build the UI (under lock) */
    ESP_LOGI(TAG, "Creating UI...");
    _lock_acquire(&lvgl_api_lock);
    {
        /* Dark screen background */
        lv_obj_t *scr = lv_display_get_screen_active(display);
        lv_obj_set_style_bg_color(scr, COL_BG, 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

        /* Left column — fixed on the screen, shared across all pages */
        ui_weather_create(scr);

        /* Right-side page container (500 px, swipeable) */
        ui_pages_create(scr);

        /* Page 1 right: compass rose + wind strip */
        lv_obj_t *p1r = ui_pages_get_right_page(0);
        ui_compass_create(p1r);
        ui_wind_strip_create(p1r);

        /* Page 2 right: indoor climate matrix */
        lv_obj_t *p2r = ui_pages_get_right_page(1);
        ui_indoor_create(p2r);

        /* Page 3 right: scene triggers + direct controls */
        lv_obj_t *p3r = ui_pages_get_right_page(2);
        ui_controls_create(p3r);
        ui_controls_set_scene_press_cb(page3_scene_press_cb);
        ui_controls_set_target_press_cb(page3_target_press_cb);

        /* Demo: show a few targets in different states */
        ui_controls_set_target_state(0, CTRL_OFF);
        ui_controls_set_target_state(1, CTRL_OFF);
        ui_controls_set_target_state(2, CTRL_OFF);
        ui_controls_set_target_state(3, CTRL_OFF);
        ui_controls_set_target_state(4, CTRL_UNAVAILABLE);
        ui_controls_set_target_state(5, CTRL_UNAVAILABLE); /* Ventilator */
        ui_controls_set_active_scene(-1);

        /* Notify the weather column when the visible page changes,
         * so the secondary line can switch between Gevoel and room RH. */
        ui_pages_set_change_cb(ui_weather_set_page);

        /* Poll WiFi status once per second and update UI indicators.
         * Runs inside lv_timer_handler, so UI calls are already locked. */
        lv_timer_create(wifi_status_timer_cb, 1000, NULL);
    }
    _lock_release(&lvgl_api_lock);

    /* 10. Start LVGL task */
    xTaskCreate(lvgl_task, "LVGL", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIO, NULL);

    /* 11. Start WiFi (after UI is up so the red indicators are visible
     *     until the first GOT_IP event flips them to normal colours). */
    net_wifi_init();

    /* 11b. NTP time sync — CET/CEST timezone (Netherlands).
     *      SNTP runs async; clock label shows "--:--" until synced. */
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);

    /* 12. Start the live panel-API fetch task. Runs in its own thread,
     *     waits for WiFi internally, acquires the LVGL lock when updating. */
    panel_api_init();

    ESP_LOGI(TAG, "Render test running. Watch the display.");
}
