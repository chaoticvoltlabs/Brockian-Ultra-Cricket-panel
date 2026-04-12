/**
 * main.c
 *
 * Entry point for the ECharts-style gauge clock on the
 * Waveshare ESP32-S3-Touch-LCD-7B (1024 × 600, GT911 touch).
 *
 * Startup sequence:
 *   1. NVS + netif init
 *   2. WiFi → STA → connect
 *   3. SNTP time sync (blocking, max 15 s)
 *   4. Set POSIX timezone
 *   5. LVGL init
 *   6. display_init()   – RGB panel + LVGL display driver
 *   7. touch_init()     – GT911 + LVGL input device
 *   8. Page UI create
 *   9. LVGL task loop (1 ms tick + lv_timer_handler every 5 ms)
 */

#include <time.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "esp_timer.h"

#include "lvgl.h"

#include "config.h"
#include "display.h"
#include "touch.h"
#include "clock_ui.h"
#include "outlook_ui.h"
#include "control_ui.h"

static const char *TAG = "main";

/* ── WiFi event group bits ──────────────────────────────────────────────────── */
#include "freertos/event_groups.h"
static EventGroupHandle_t s_wifi_eg;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry = 0;
#define MAX_RETRY 10

/* ─────────────────────────────────────────────────────────────────────────────
 * WiFi event handler
 * ───────────────────────────────────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < MAX_RETRY) {
            esp_wifi_connect();
            s_retry++;
            ESP_LOGW(TAG, "WiFi retry %d/%d", s_retry, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * WiFi STA connect (blocking)
 * ───────────────────────────────────────────────────────────────────────────── */
static bool wifi_connect(void)
{
    s_wifi_eg = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_got_ip));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                           pdFALSE, pdFALSE,
                           pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
        return true;
    }
    ESP_LOGE(TAG, "WiFi connection failed");
    return false;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * SNTP sync (blocking, max 15 s)
 * ───────────────────────────────────────────────────────────────────────────── */
static void sntp_sync(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    esp_sntp_init();

    int attempts = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && attempts < 30) {
        ESP_LOGI(TAG, "Waiting for NTP sync… (%d)", attempts);
        vTaskDelay(pdMS_TO_TICKS(500));
        attempts++;
    }
    setenv("TZ", TZ_STRING, 1);
    tzset();

    time_t now;
    time(&now);
    char buf[32];
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    ESP_LOGI(TAG, "Local time: %s", buf);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * LVGL task – runs lv_timer_handler and updates the clock every second
 * ───────────────────────────────────────────────────────────────────────────── */
static SemaphoreHandle_t s_lvgl_mutex;

static void lvgl_task(void *arg)
{
    TickType_t last_tick_update = xTaskGetTickCount();
    time_t last_clock_sec = (time_t)-1;

    while (1) {
        /* Feed LVGL tick (1 ms resolution via lv_tick_inc) */
        TickType_t now = xTaskGetTickCount();
        uint32_t elapsed_ms = (uint32_t)((now - last_tick_update) *
                                         portTICK_PERIOD_MS);
        if (elapsed_ms > 0) {
            lv_tick_inc(elapsed_ms);
            last_tick_update = now;
        }

        /* Run LVGL internal timers */
        if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(s_lvgl_mutex);
        }

        /* Update clock exactly when wall-clock seconds change */
        time_t wall_now;
        time(&wall_now);
        if (wall_now != last_clock_sec) {
            if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                clock_ui_tick();
                xSemaphoreGive(s_lvgl_mutex);
                last_clock_sec = wall_now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * app_main
 * ───────────────────────────────────────────────────────────────────────────── */
void app_main(void)
{
    /* Ultra-early ROM printf – visible even before console init */
    esp_rom_printf("\r\n*** app_main reached ***\r\n");

    /* ── NVS ──────────────────────────────────────────────────────────────── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── WiFi + NTP ────────────────────────────────────────────────────────── */
    if (wifi_connect()) {
        sntp_sync();
    } else {
        ESP_LOGW(TAG, "Running without NTP – clock shows epoch time");
        setenv("TZ", TZ_STRING, 1);
        tzset();
    }

    /* ── LVGL ─────────────────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "lv_init...");
    lv_init();

    /* ── Display + Touch ──────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "display_init...");
    display_init(NULL);

    ESP_LOGI(TAG, "touch_init...");
    touch_init();
    backlight_set_percent(BACKLIGHT_DEFAULT_PERCENT);

    /* ── Page UI ──────────────────────────────────────────────────────────── */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x090909), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *tv = lv_tileview_create(scr);
    lv_obj_set_size(tv, LCD_H_RES, LCD_V_RES);
    lv_obj_center(tv);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x090909), 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tv, 0, 0);
    lv_obj_set_style_pad_all(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *page_clock = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(page_clock, lv_color_hex(0x090909), 0);
    lv_obj_set_style_bg_opa(page_clock, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_clock, 0, 0);
    lv_obj_set_style_pad_all(page_clock, 0, 0);
    lv_obj_clear_flag(page_clock, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *page_outlook = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(page_outlook, lv_color_hex(0x090909), 0);
    lv_obj_set_style_bg_opa(page_outlook, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_outlook, 0, 0);
    lv_obj_set_style_pad_all(page_outlook, 0, 0);
    lv_obj_clear_flag(page_outlook, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *page_control = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT);
    lv_obj_set_style_bg_color(page_control, lv_color_hex(0x090909), 0);
    lv_obj_set_style_bg_opa(page_control, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_control, 0, 0);
    lv_obj_set_style_pad_all(page_control, 0, 0);
    lv_obj_clear_flag(page_control, LV_OBJ_FLAG_SCROLLABLE);

    ESP_LOGI(TAG, "clock_ui_create...");
    clock_ui_create(page_clock);
    clock_ui_set_room_context("Mancave", 21.4f, 43);

    ESP_LOGI(TAG, "outlook_ui_create...");
    outlook_ui_create(page_outlook);

    ESP_LOGI(TAG, "control_ui_create...");
    control_ui_create(page_control);

    ESP_LOGI(TAG, "clock_ui_tick...");
    clock_ui_tick();   /* paint immediately so there's no blank frame */

    /* ── LVGL task ────────────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "starting LVGL task...");
    s_lvgl_mutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(lvgl_task, "lvgl",
                            LVGL_TASK_STACK, NULL,
                            LVGL_TASK_PRIO, NULL,
                            0 /* pin to core 0: same core as GDMA ISR,
                                 * avoids cross-core L1 D-cache incoherency
                                 * on the PSRAM framebuffer */);

    ESP_LOGI(TAG, "Boot complete");
}
