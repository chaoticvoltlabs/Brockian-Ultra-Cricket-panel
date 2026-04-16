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
#include <stdio.h>
#include <string.h>
#include <strings.h>

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
#include "esp_mac.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"

#include "lvgl.h"
#include "cJSON.h"

#include "config.h"
#include "display.h"
#include "touch.h"
#include "clock_ui.h"
#include "outlook_ui.h"
#include "control_ui.h"
#include "control_post.h"
#include "indoor_ui.h"
#include "weather_col.h"
#include "panel_alert.h"

static const char *TAG = "main";

/* ── WiFi event group bits ──────────────────────────────────────────────────── */
#include "freertos/event_groups.h"
static EventGroupHandle_t s_wifi_eg;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry = 0;
#define MAX_RETRY 10

#define WEATHER_FETCH_INTERVAL_MS 10000
#define WEATHER_RETRY_MS           2000
#define WEATHER_RESP_MAX           4096
#define FORECAST_RESP_MAX          (12 * 1024)

static char s_weather_resp[WEATHER_RESP_MAX];
static int s_weather_resp_len = 0;
static char *s_forecast_resp = NULL;
static int  s_forecast_resp_len = 0;
static char s_panel_mac_hdr[18] = "";
static char s_panel_ip_hdr[16] = "";

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

static esp_err_t weather_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (evt->user_data == NULL || evt->data == NULL || evt->data_len <= 0) {
            return ESP_OK;
        }

        char *buf = evt->user_data;
        size_t cap = WEATHER_RESP_MAX;
        size_t room = (s_weather_resp_len < (int)cap) ? (cap - (size_t)s_weather_resp_len - 1U) : 0U;
        size_t copy_len = (evt->data_len < (int)room) ? (size_t)evt->data_len : room;
        if (copy_len > 0) {
            memcpy(buf + s_weather_resp_len, evt->data, copy_len);
            s_weather_resp_len += (int)copy_len;
            buf[s_weather_resp_len] = '\0';
        }
    }
    return ESP_OK;
}

static bool fetch_weather_once(void)
{
    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d%s", BUC_HOST, BUC_PORT, BUC_API_END_POINT);

    s_weather_resp_len = 0;
    s_weather_resp[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 4000,
        .event_handler = weather_http_event,
        .user_data = s_weather_resp,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return false;
    }

    if (s_panel_mac_hdr[0] != '\0') {
        esp_http_client_set_header(client, "X-Panel-MAC", s_panel_mac_hdr);
    }
    if (s_panel_ip_hdr[0] != '\0') {
        esp_http_client_set_header(client, "X-Panel-IP", s_panel_ip_hdr);
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);

    return err == ESP_OK && status == 200 && s_weather_resp_len > 0;
}

static bool json_number(cJSON *obj, const char *key, double *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

static bool json_rgb(cJSON *obj, const char *key, int rgb[3])
{
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) != 3) {
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsNumber(item)) {
            return false;
        }
        int v = (int)item->valuedouble;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        rgb[i] = v;
    }
    return true;
}

static void parse_and_apply_weather(void)
{
    cJSON *root = cJSON_Parse(s_weather_resp);
    if (root == NULL) {
        return;
    }

    double gust_bft = 0.0;
    bool have_gust = json_number(root, "gust_bft", &gust_bft);

    double wind_bft = 0.0;
    double dir_deg  = 0.0;
    double wind_kmh = 0.0;
    bool have_wind_bft = json_number(root, "wind_bft", &wind_bft);
    bool have_dir      = json_number(root, "wind_dir_deg", &dir_deg);
    bool have_wind_kmh = json_number(root, "wind_kmh", &wind_kmh);

    double outside_temp_c = 0.0, feels_like_c = 0.0, hum_pct = 0.0, pressure_hpa = 0.0;
    bool have_outside_temp = json_number(root, "outside_temp_c", &outside_temp_c);
    bool have_feels_like   = json_number(root, "feels_like_c",   &feels_like_c);
    bool have_hum          = json_number(root, "humidity_pct",   &hum_pct);
    bool have_pressure     = json_number(root, "pressure_hpa",   &pressure_hpa);
    double ambient_brightness_pct = 0.0;
    bool have_ambient_brightness = json_number(root, "ambient_brightness_pct", &ambient_brightness_pct);
    int ambient_rgb[3] = {0};
    bool have_ambient_rgb = json_rgb(root, "ambient_rgb", ambient_rgb);

    float baro_trend[60];
    int   baro_trend_len = 0;
    cJSON *trend = cJSON_GetObjectItemCaseSensitive(root, "pressure_trend_24h");
    if (cJSON_IsArray(trend)) {
        int n = cJSON_GetArraySize(trend);
        int start = (n > (int)(sizeof baro_trend / sizeof baro_trend[0]))
                    ? n - (int)(sizeof baro_trend / sizeof baro_trend[0]) : 0;
        for (int i = start; i < n; ++i) {
            cJSON *v = cJSON_GetArrayItem(trend, i);
            if (cJSON_IsNumber(v)) {
                baro_trend[baro_trend_len++] = (float)v->valuedouble;
            }
        }
    }

    double temp_c = 0.0;
    double rh_pct = 0.0;
    bool have_temp = false;
    bool have_rh = false;

    cJSON *zones = cJSON_GetObjectItemCaseSensitive(root, "indoor_zones");
    if (cJSON_IsArray(zones)) {
        cJSON *zone = cJSON_GetArrayItem(zones, PANEL_ROOM_ZONE_INDEX);
        if (cJSON_IsObject(zone)) {
            have_temp = json_number(zone, "temp_c", &temp_c);
            have_rh = json_number(zone, "rh_pct", &rh_pct);
        }
    }

    cJSON *target_states = cJSON_GetObjectItemCaseSensitive(root, "page3_target_states");

    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (have_temp && have_rh) {
            clock_ui_set_room_context("Studio", (float)temp_c, (int)(rh_pct + 0.5));
            control_ui_set_room_climate((float)temp_c, true, (float)rh_pct, true);
        }
        if (have_gust) {
            panel_alert_set_storm_active(gust_bft >= STORM_ALERT_GUST_BFT);
        }
        if (have_wind_bft && have_gust && have_dir) {
            outlook_ui_set_current((int)wind_bft, (int)gust_bft, (int)dir_deg);
        }
        weather_col_set_outdoor((float)outside_temp_c, have_outside_temp,
                                (float)feels_like_c,   have_feels_like,
                                (int)(hum_pct + 0.5),  have_hum);
        if (have_wind_bft || have_gust) {
            weather_col_set_wind(have_wind_bft ? (int)wind_bft : 0,
                                 have_gust ? (int)gust_bft : 0,
                                 have_wind_kmh ? (int)(wind_kmh + 0.5) : 0);
        }
        weather_col_set_pressure((int)(pressure_hpa + 0.5), have_pressure,
                                 baro_trend_len > 0 ? baro_trend : NULL,
                                 baro_trend_len);
        if (cJSON_IsArray(zones)) {
            for (int i = 0; i < INDOOR_ZONES; ++i) {
                cJSON *z = cJSON_GetArrayItem(zones, i);
                double t = 0.0, h = 0.0;
                bool t_ok = cJSON_IsObject(z) && json_number(z, "temp_c", &t);
                bool h_ok = cJSON_IsObject(z) && json_number(z, "rh_pct", &h);
                indoor_ui_set_zone(i, (float)t, (float)h, t_ok, h_ok);
            }
        }
        if (cJSON_IsObject(target_states)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, target_states) {
                if (!cJSON_IsString(item) || item->string == NULL) continue;
                const char *val = item->valuestring;
                if (val == NULL) continue;
                if (strcmp(val, "on") == 0) {
                    control_ui_set_target_state_by_name(item->string, CTRL_ON);
                } else if (strcmp(val, "off") == 0) {
                    control_ui_set_target_state_by_name(item->string, CTRL_OFF);
                }
                /* "unavailable" intentionally ignored: scene-backed targets
                 * have no stable state, so we leave the switch as-is rather
                 * than greying it out on every poll. */
            }
        }
        if (have_ambient_brightness) {
            control_ui_set_ambient_brightness((int)(ambient_brightness_pct + 0.5));
        }
        if (have_ambient_rgb) {
            control_ui_set_ambient_rgb(ambient_rgb[0], ambient_rgb[1], ambient_rgb[2]);
        }
        xSemaphoreGive(s_lvgl_mutex);
    }

    cJSON_Delete(root);
}

static esp_err_t forecast_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (evt->user_data == NULL || evt->data == NULL || evt->data_len <= 0) {
            return ESP_OK;
        }
        char *buf = evt->user_data;
        size_t cap = FORECAST_RESP_MAX;
        size_t room = (s_forecast_resp_len < (int)cap) ? (cap - (size_t)s_forecast_resp_len - 1U) : 0U;
        size_t copy_len = (evt->data_len < (int)room) ? (size_t)evt->data_len : room;
        if (copy_len > 0) {
            memcpy(buf + s_forecast_resp_len, evt->data, copy_len);
            s_forecast_resp_len += (int)copy_len;
            buf[s_forecast_resp_len] = '\0';
        }
    }
    return ESP_OK;
}

static bool fetch_forecast_once(void)
{
    if (s_forecast_resp == NULL) return false;

    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d%s", BUC_HOST, BUC_PORT, BUC_API_FORECAST_ENDPOINT);

    s_forecast_resp_len = 0;
    s_forecast_resp[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 4000,
        .event_handler = forecast_http_event,
        .user_data = s_forecast_resp,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) return false;

    if (s_panel_mac_hdr[0] != '\0') {
        esp_http_client_set_header(client, "X-Panel-MAC", s_panel_mac_hdr);
    }
    if (s_panel_ip_hdr[0] != '\0') {
        esp_http_client_set_header(client, "X-Panel-IP", s_panel_ip_hdr);
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);

    return err == ESP_OK && status == 200 && s_forecast_resp_len > 0;
}

static time_t parse_iso_minute(const char *s)
{
    if (s == NULL) return 0;
    int yr, mo, dy, hr, mn;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d", &yr, &mo, &dy, &hr, &mn) != 5) {
        return 0;
    }
    struct tm tm = {0};
    tm.tm_year  = yr - 1900;
    tm.tm_mon   = mo - 1;
    tm.tm_mday  = dy;
    tm.tm_hour  = hr;
    tm.tm_min   = mn;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

static void parse_and_apply_forecast(void)
{
    if (s_forecast_resp == NULL || s_forecast_resp_len <= 0) return;

    cJSON *root = cJSON_Parse(s_forecast_resp);
    if (root == NULL) return;

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON *arr  = cJSON_GetObjectItemCaseSensitive(data, "forecast");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return; }

    int n = cJSON_GetArraySize(arr);
    if (n <= 0) { cJSON_Delete(root); return; }

    time_t now_ts = time(NULL);
    bool ntp_ok = (now_ts >= 1700000000);

    int base_idx = 0;
    if (ntp_ok) {
        time_t best_delta = 0;
        bool first = true;
        for (int i = 0; i < n; ++i) {
            cJSON *e = cJSON_GetArrayItem(arr, i);
            cJSON *t = cJSON_GetObjectItemCaseSensitive(e, "t");
            if (!cJSON_IsString(t)) continue;
            time_t ts = parse_iso_minute(t->valuestring);
            if (ts == 0) continue;
            time_t delta = (ts >= now_ts) ? (ts - now_ts) : (now_ts - ts);
            if (first || delta < best_delta) {
                best_delta = delta;
                base_idx = i;
                first = false;
            }
        }
    }

    static const int sample_hrs[8] = {0, 6, 12, 18, 24, 30, 36, 42};
    int wind8[8] = {0};
    int gust8[8] = {0};
    for (int s = 0; s < 8; ++s) {
        int idx = base_idx + sample_hrs[s];
        if (idx >= n) idx = n - 1;
        cJSON *e = cJSON_GetArrayItem(arr, idx);
        double ws = 0.0, wg = 0.0;
        json_number(e, "ws_bft", &ws);
        json_number(e, "wg_bft", &wg);
        wind8[s] = (int)ws;
        gust8[s] = (int)wg;
    }

    int peak_bft = 0;
    int peak_offset = 0;
    int window_end = base_idx + 48;
    if (window_end > n) window_end = n;
    for (int i = base_idx; i < window_end; ++i) {
        cJSON *e = cJSON_GetArrayItem(arr, i);
        double wg = 0.0;
        if (json_number(e, "wg_bft", &wg) && (int)wg > peak_bft) {
            peak_bft = (int)wg;
            if (ntp_ok) {
                cJSON *t = cJSON_GetObjectItemCaseSensitive(e, "t");
                if (cJSON_IsString(t)) {
                    time_t ts = parse_iso_minute(t->valuestring);
                    peak_offset = (ts > now_ts) ? (int)((ts - now_ts) / 3600) : 0;
                }
            } else {
                peak_offset = i - base_idx;
            }
        }
    }

    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        outlook_ui_set_forecast(wind8, gust8, peak_bft, peak_offset);
        xSemaphoreGive(s_lvgl_mutex);
    }

    cJSON_Delete(root);
}

static void live_weather_task(void *arg)
{
    LV_UNUSED(arg);

    if (s_forecast_resp == NULL) {
        s_forecast_resp = heap_caps_malloc(FORECAST_RESP_MAX, MALLOC_CAP_SPIRAM);
    }
    if (s_forecast_resp == NULL) {
        ESP_LOGW(TAG, "forecast buffer alloc failed - forecast will be skipped");
    }

    while (1) {
        if (fetch_weather_once()) {
            parse_and_apply_weather();
            if (fetch_forecast_once()) {
                parse_and_apply_forecast();
            }
            vTaskDelay(pdMS_TO_TICKS(WEATHER_FETCH_INTERVAL_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(WEATHER_RETRY_MS));
        }
    }
}

static void get_panel_identity(char *mac, size_t mac_len, char *ip, size_t ip_len)
{
    if (mac != NULL && mac_len > 0) {
        uint8_t mac_raw[6];
        if (esp_read_mac(mac_raw, ESP_MAC_WIFI_STA) == ESP_OK) {
            snprintf(mac, mac_len, "%02x:%02x:%02x:%02x:%02x:%02x",
                     mac_raw[0], mac_raw[1], mac_raw[2],
                     mac_raw[3], mac_raw[4], mac_raw[5]);
        } else {
            mac[0] = '\0';
        }
    }

    if (ip != NULL && ip_len > 0) {
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(ip, ip_len, IPSTR, IP2STR(&ip_info.ip));
        } else {
            ip[0] = '\0';
        }
    }
}

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
                weather_col_tick();
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
    char panel_mac[18] = "";
    char panel_ip[16] = "";
    bool wifi_ok = false;

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
    wifi_ok = wifi_connect();
    if (wifi_ok) {
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

    s_lvgl_mutex = xSemaphoreCreateMutex();

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

    lv_obj_t *page_control = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(page_control, lv_color_hex(0x090909), 0);
    lv_obj_set_style_bg_opa(page_control, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_control, 0, 0);
    lv_obj_set_style_pad_all(page_control, 0, 0);
    lv_obj_clear_flag(page_control, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *page_indoor = lv_tileview_add_tile(tv, 3, 0, LV_DIR_LEFT);
    lv_obj_set_style_bg_color(page_indoor, lv_color_hex(0x090909), 0);
    lv_obj_set_style_bg_opa(page_indoor, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_indoor, 0, 0);
    lv_obj_set_style_pad_all(page_indoor, 0, 0);
    lv_obj_clear_flag(page_indoor, LV_OBJ_FLAG_SCROLLABLE);

    ESP_LOGI(TAG, "clock_ui_create...");
    clock_ui_create(page_clock);
    clock_ui_set_room_context("Studio", 21.4f, 43);

    ESP_LOGI(TAG, "outlook_ui_create...");
    outlook_ui_create(page_outlook);

    ESP_LOGI(TAG, "control_ui_create...");
    control_ui_create(page_control);

    ESP_LOGI(TAG, "indoor_ui_create...");
    indoor_ui_create(page_indoor);

    ESP_LOGI(TAG, "weather_col_create...");
    weather_col_create(page_indoor);
    weather_col_tick();
    get_panel_identity(panel_mac, sizeof(panel_mac), panel_ip, sizeof(panel_ip));
    strncpy(s_panel_mac_hdr, panel_mac, sizeof(s_panel_mac_hdr) - 1);
    strncpy(s_panel_ip_hdr, panel_ip, sizeof(s_panel_ip_hdr) - 1);
    control_ui_set_debug_identity(panel_ip, panel_mac);

    control_post_set_identity(panel_mac, panel_ip);
    control_post_init();

    panel_alert_set_storm_active(false);

    ESP_LOGI(TAG, "clock_ui_tick...");
    clock_ui_tick();   /* paint immediately so there's no blank frame */

    if (wifi_ok) {
        /* Hydrate page state once before the user starts interacting.
         * This prevents page-3 switches from starting in a stale visual state
         * and then "toggling the wrong way" on the first tap. */
        if (fetch_weather_once()) {
            parse_and_apply_weather();
            s_forecast_resp = heap_caps_malloc(FORECAST_RESP_MAX, MALLOC_CAP_SPIRAM);
            if (s_forecast_resp != NULL && fetch_forecast_once()) {
                parse_and_apply_forecast();
            }
        }
    }

    /* ── LVGL task ────────────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "starting LVGL task...");
    xTaskCreatePinnedToCore(lvgl_task, "lvgl",
                            LVGL_TASK_STACK, NULL,
                            LVGL_TASK_PRIO, NULL,
                            0 /* pin to core 0: same core as GDMA ISR,
                                 * avoids cross-core L1 D-cache incoherency
                                 * on the PSRAM framebuffer */);

    if (wifi_ok) {
        xTaskCreatePinnedToCore(live_weather_task, "weather_live",
                                8 * 1024, NULL, 2, NULL, 0);
    }

    ESP_LOGI(TAG, "Boot complete");
}
