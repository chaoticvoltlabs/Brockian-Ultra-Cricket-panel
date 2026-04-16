/**
 * @file  panel_api.c
 * @brief Fetches live weather JSON from the upstream BUC API.
 *
 * Runs in its own FreeRTOS task. Uses esp_http_client (blocking) so it
 * cannot live inside an LVGL timer. After each successful fetch it
 * acquires the LVGL API lock and calls the UI update functions.
 *
 * Endpoint (host, port and path all come from secrets.h):
 *   GET http://<BUC_HOST>:<BUC_PORT><BUC_API_END_POINT>
 *
 * Expected JSON schema:
 *   {
 *     "outside_temp_c":      float,
 *     "feels_like_c":        float,
 *     "wind_bft":            int,
 *     "wind_kmh":            int,      (not used -- recomputed from bft)
 *     "gust_bft":            int,
 *     "gust_kmh":            int,      (not used)
 *     "wind_dir_deg":        int,
 *     "humidity_pct":        int,
 *     "pressure_hpa":        int,
 *     "pressure_trend_24h":  [float, ...]   (N values, oldest -> newest)
 *     "night_mode":         bool,      (optional, theme switch from HA)
 *     "indoor_zones":        [              (optional, up to 12 entries)
 *       { "temp_c": float, "rh_pct": float },   (null fields = missing)
 *       ...
 *     ]
 *   }
 */

#include "panel_api.h"
#include "secrets.h"
#include "demo_data.h"          /* demo_data_t struct -- reused as the */
                                /* neutral "weather sample" carrier    */
#include "ui_weather.h"
#include "ui_compass.h"
#include "ui_wind_strip.h"
#include "ui_indoor.h"
#include "ui_controls.h"
#include "ui_theme.h"
#include "net_wifi.h"
#include "app_lvgl_lock.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "cJSON.h"

static const char *TAG = "panel_api";

/* ── Endpoint config ────────────────────────────────────────────────── */
/* Host, port and path all come from secrets.h (BUC_HOST, BUC_PORT,
 * BUC_API_END_POINT) so the upstream can be repointed without touching
 * code. */

/* Poll interval: weather doesn't change fast and this is a test API,
 * so 10 s gives plenty of update cadence without hammering the server. */
#define FETCH_INTERVAL_MS   10000

/* Retry cadence after a failed fetch or while WiFi is down. Short enough
 * that the UI updates promptly once connectivity is restored. */
#define RETRY_MS             2000

#define RESP_MAX             2048   /* upper bound on the JSON response */
#define MAX_BARO_POINTS        64   /* matches BARO_HISTORY in ui_weather */
#define CONTROL_QUEUE_LEN       4
#define CONTROL_FIELD_MAX      32
#define PANEL_MAC_MAX          18
#define PANEL_IP_MAX           16
#define PANEL_SCENE_SLOTS       4
#define PANEL_TARGET_SLOTS      6
#define STORM_ALERT_GUST_BFT  8.0f

typedef struct {
    char label[24];
    char target[CONTROL_FIELD_MAX];
    char action[CONTROL_FIELD_MAX];
    bool enabled;
} panel_slot_config_t;

typedef struct {
    char target[CONTROL_FIELD_MAX];
    char action[CONTROL_FIELD_MAX];
} panel_control_command_t;

typedef struct {
    panel_slot_config_t scenes[PANEL_SCENE_SLOTS];
    panel_slot_config_t targets[PANEL_TARGET_SLOTS];
    panel_control_command_t long_press;
} panel_profile_config_t;

/* ── Response accumulator (owned by the fetch task) ────────────────── */
static char s_resp[RESP_MAX];
static int  s_resp_len;

/* Task lifecycle */
static bool s_started = false;
static QueueHandle_t s_control_queue = NULL;
static panel_profile_config_t s_profile_cfg = {0};

static void init_default_profile(void)
{
    memset(&s_profile_cfg, 0, sizeof(s_profile_cfg));

    snprintf(s_profile_cfg.scenes[0].label, sizeof(s_profile_cfg.scenes[0].label), "%s", "Work");
    snprintf(s_profile_cfg.scenes[0].target, sizeof(s_profile_cfg.scenes[0].target), "%s", "scene_work");
    snprintf(s_profile_cfg.scenes[0].action, sizeof(s_profile_cfg.scenes[0].action), "%s", "activate");
    s_profile_cfg.scenes[0].enabled = true;

    snprintf(s_profile_cfg.scenes[1].label, sizeof(s_profile_cfg.scenes[1].label), "%s", "Evening");
    snprintf(s_profile_cfg.scenes[1].target, sizeof(s_profile_cfg.scenes[1].target), "%s", "scene_evening");
    snprintf(s_profile_cfg.scenes[1].action, sizeof(s_profile_cfg.scenes[1].action), "%s", "activate");
    s_profile_cfg.scenes[1].enabled = true;

    snprintf(s_profile_cfg.scenes[2].label, sizeof(s_profile_cfg.scenes[2].label), "%s", "Movie");
    snprintf(s_profile_cfg.scenes[2].target, sizeof(s_profile_cfg.scenes[2].target), "%s", "scene_movie");
    snprintf(s_profile_cfg.scenes[2].action, sizeof(s_profile_cfg.scenes[2].action), "%s", "activate");
    s_profile_cfg.scenes[2].enabled = true;

    snprintf(s_profile_cfg.scenes[3].label, sizeof(s_profile_cfg.scenes[3].label), "%s", "Night");
    snprintf(s_profile_cfg.scenes[3].target, sizeof(s_profile_cfg.scenes[3].target), "%s", "scene_night");
    snprintf(s_profile_cfg.scenes[3].action, sizeof(s_profile_cfg.scenes[3].action), "%s", "activate");
    s_profile_cfg.scenes[3].enabled = true;

    snprintf(s_profile_cfg.targets[0].label, sizeof(s_profile_cfg.targets[0].label), "%s", "Light A");
    snprintf(s_profile_cfg.targets[0].target, sizeof(s_profile_cfg.targets[0].target), "%s", "light_a");
    snprintf(s_profile_cfg.targets[0].action, sizeof(s_profile_cfg.targets[0].action), "%s", "toggle");
    s_profile_cfg.targets[0].enabled = true;

    snprintf(s_profile_cfg.targets[1].label, sizeof(s_profile_cfg.targets[1].label), "%s", "Light B");
    snprintf(s_profile_cfg.targets[1].target, sizeof(s_profile_cfg.targets[1].target), "%s", "light_b");
    snprintf(s_profile_cfg.targets[1].action, sizeof(s_profile_cfg.targets[1].action), "%s", "toggle");
    s_profile_cfg.targets[1].enabled = true;

    snprintf(s_profile_cfg.targets[2].label, sizeof(s_profile_cfg.targets[2].label), "%s", "Light C");
    snprintf(s_profile_cfg.targets[2].target, sizeof(s_profile_cfg.targets[2].target), "%s", "light_c");
    snprintf(s_profile_cfg.targets[2].action, sizeof(s_profile_cfg.targets[2].action), "%s", "toggle");
    s_profile_cfg.targets[2].enabled = true;

    snprintf(s_profile_cfg.targets[3].label, sizeof(s_profile_cfg.targets[3].label), "%s", "Media");
    snprintf(s_profile_cfg.targets[3].target, sizeof(s_profile_cfg.targets[3].target), "%s", "media_power");
    snprintf(s_profile_cfg.targets[3].action, sizeof(s_profile_cfg.targets[3].action), "%s", "toggle");
    s_profile_cfg.targets[3].enabled = true;

    snprintf(s_profile_cfg.targets[4].label, sizeof(s_profile_cfg.targets[4].label), "%s", "");
    snprintf(s_profile_cfg.targets[4].target, sizeof(s_profile_cfg.targets[4].target), "%s", "");
    snprintf(s_profile_cfg.targets[4].action, sizeof(s_profile_cfg.targets[4].action), "%s", "");
    s_profile_cfg.targets[4].enabled = false;

    snprintf(s_profile_cfg.targets[5].label, sizeof(s_profile_cfg.targets[5].label), "%s", "");
    snprintf(s_profile_cfg.targets[5].target, sizeof(s_profile_cfg.targets[5].target), "%s", "");
    snprintf(s_profile_cfg.targets[5].action, sizeof(s_profile_cfg.targets[5].action), "%s", "");
    s_profile_cfg.targets[5].enabled = false;

    snprintf(s_profile_cfg.long_press.target, sizeof(s_profile_cfg.long_press.target), "%s", "scene_night");
    snprintf(s_profile_cfg.long_press.action, sizeof(s_profile_cfg.long_press.action), "%s", "activate");
}

static void apply_profile_to_ui(void)
{
    for (int i = 0; i < PANEL_SCENE_SLOTS; i++) {
        ui_controls_set_scene_slot(i, s_profile_cfg.scenes[i].label, s_profile_cfg.scenes[i].enabled);
    }

    for (int i = 0; i < PANEL_TARGET_SLOTS; i++) {
        ui_controls_set_target_label(i, s_profile_cfg.targets[i].label);
        ui_controls_set_target_state(i, s_profile_cfg.targets[i].enabled ? CTRL_OFF : CTRL_UNAVAILABLE);
    }

    ui_controls_set_active_scene(-1);
}

static void get_panel_identity(char *mac, size_t mac_len, char *ip, size_t ip_len)
{
    if (mac != NULL && mac_len > 0) {
        net_wifi_get_mac_string(mac, mac_len);
    }
    if (ip != NULL && ip_len > 0) {
        net_wifi_get_ipv4_string(ip, ip_len);
    }
}

/* ── HTTP event callback: accumulate body into s_resp ──────────────── */
static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        int remaining = (int)sizeof(s_resp) - 1 - s_resp_len;
        int take = (evt->data_len < remaining) ? evt->data_len : remaining;
        if (take > 0) {
            memcpy(s_resp + s_resp_len, evt->data, take);
            s_resp_len += take;
        }
    }
    return ESP_OK;
}

/* ── Perform one GET, return status code or -1 on transport error ──── */
static int fetch_once(void)
{
    char url[128];
    char mac[PANEL_MAC_MAX] = "";
    char ip[PANEL_IP_MAX] = "";
    snprintf(url, sizeof(url), "http://%s:%d%s",
             BUC_HOST, BUC_PORT, BUC_API_END_POINT);
    get_panel_identity(mac, sizeof(mac), ip, sizeof(ip));

    s_resp_len = 0;

    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = http_event,
        .timeout_ms    = 6000,
        .buffer_size   = 1024,
    };

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) {
        ESP_LOGE(TAG, "client init failed");
        return -1;
    }

    if (mac[0] != '\0') esp_http_client_set_header(h, "X-Panel-MAC", mac);
    if (ip[0] != '\0') esp_http_client_set_header(h, "X-Panel-IP", ip);

    esp_err_t err = esp_http_client_perform(h);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(h) : -1;
    esp_http_client_cleanup(h);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "perform failed: %s", esp_err_to_name(err));
        return -1;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP %d", status);
        return status;
    }

    s_resp[s_resp_len] = '\0';
    return 200;
}

static bool parse_panel_config(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "panel config parse failed");
        return false;
    }

    cJSON *page3 = cJSON_GetObjectItemCaseSensitive(root, "page3");
    cJSON *scenes = cJSON_IsObject(page3) ? cJSON_GetObjectItemCaseSensitive(page3, "scenes") : NULL;
    cJSON *targets = cJSON_IsObject(page3) ? cJSON_GetObjectItemCaseSensitive(page3, "targets") : NULL;
    cJSON *longPress = cJSON_IsObject(page3) ? cJSON_GetObjectItemCaseSensitive(page3, "long_press") : NULL;

    panel_profile_config_t next = {0};

    if (cJSON_IsArray(scenes)) {
        int n = cJSON_GetArraySize(scenes);
        if (n > PANEL_SCENE_SLOTS) n = PANEL_SCENE_SLOTS;
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(scenes, i);
            cJSON *label = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, "label") : NULL;
            cJSON *target = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, "target") : NULL;
            cJSON *action = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, "action") : NULL;
            if (cJSON_IsString(label) && cJSON_IsString(target) && cJSON_IsString(action)) {
                snprintf(next.scenes[i].label, sizeof(next.scenes[i].label), "%s", label->valuestring);
                snprintf(next.scenes[i].target, sizeof(next.scenes[i].target), "%s", target->valuestring);
                snprintf(next.scenes[i].action, sizeof(next.scenes[i].action), "%s", action->valuestring);
                next.scenes[i].enabled = true;
            }
        }
    }

    if (cJSON_IsArray(targets)) {
        int n = cJSON_GetArraySize(targets);
        if (n > PANEL_TARGET_SLOTS) n = PANEL_TARGET_SLOTS;
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(targets, i);
            cJSON *label = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, "label") : NULL;
            cJSON *target = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, "target") : NULL;
            cJSON *action = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, "action") : NULL;
            if (cJSON_IsString(label) && cJSON_IsString(target) && cJSON_IsString(action)) {
                snprintf(next.targets[i].label, sizeof(next.targets[i].label), "%s", label->valuestring);
                snprintf(next.targets[i].target, sizeof(next.targets[i].target), "%s", target->valuestring);
                snprintf(next.targets[i].action, sizeof(next.targets[i].action), "%s", action->valuestring);
                next.targets[i].enabled = true;
            }
        }
    }

    if (cJSON_IsObject(longPress)) {
        cJSON *target = cJSON_GetObjectItemCaseSensitive(longPress, "target");
        cJSON *action = cJSON_GetObjectItemCaseSensitive(longPress, "action");
        if (cJSON_IsString(target) && cJSON_IsString(action)) {
            snprintf(next.long_press.target, sizeof(next.long_press.target), "%s", target->valuestring);
            snprintf(next.long_press.action, sizeof(next.long_press.action), "%s", action->valuestring);
        }
    }

    cJSON_Delete(root);
    s_profile_cfg = next;

    _lock_acquire(&lvgl_api_lock);
    apply_profile_to_ui();
    _lock_release(&lvgl_api_lock);

    ESP_LOGI(TAG, "panel config applied");
    return true;
}

static int fetch_panel_config_once(void)
{
    char url[128];
    char mac[PANEL_MAC_MAX] = "";
    char ip[PANEL_IP_MAX] = "";
    snprintf(url, sizeof(url), "http://%s:%d/api/panel/config", BUC_HOST, BUC_PORT);
    get_panel_identity(mac, sizeof(mac), ip, sizeof(ip));

    s_resp_len = 0;

    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = http_event,
        .timeout_ms    = 6000,
        .buffer_size   = 1024,
    };

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) {
        ESP_LOGE(TAG, "config client init failed");
        return -1;
    }

    if (mac[0] != '\0') esp_http_client_set_header(h, "X-Panel-MAC", mac);
    if (ip[0] != '\0') esp_http_client_set_header(h, "X-Panel-IP", ip);

    esp_err_t err = esp_http_client_perform(h);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(h) : -1;
    esp_http_client_cleanup(h);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "config perform failed: %s", esp_err_to_name(err));
        return -1;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "config HTTP %d", status);
        return status;
    }

    s_resp[s_resp_len] = '\0';
    return 200;
}

static int post_control_once(const panel_control_command_t *cmd)
{
    char url[128];
    char body[160];
    char mac[PANEL_MAC_MAX] = "";
    char ip[PANEL_IP_MAX] = "";

    if (cmd == NULL) {
        return -1;
    }

    snprintf(url, sizeof(url), "http://%s:%d/api/panel/control", BUC_HOST, BUC_PORT);
    snprintf(body, sizeof(body),
             "{\"target\":\"%s\",\"action\":\"%s\"}",
             cmd->target, cmd->action);
    get_panel_identity(mac, sizeof(mac), ip, sizeof(ip));

    esp_http_client_config_t cfg = {
        .url        = url,
        .timeout_ms = 6000,
        .buffer_size = 512,
    };

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) {
        ESP_LOGE(TAG, "control client init failed");
        return -1;
    }

    esp_http_client_set_method(h, HTTP_METHOD_POST);
    esp_http_client_set_header(h, "Content-Type", "application/json");
    if (mac[0] != '\0') esp_http_client_set_header(h, "X-Panel-MAC", mac);
    if (ip[0] != '\0') esp_http_client_set_header(h, "X-Panel-IP", ip);
    esp_http_client_set_post_field(h, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(h);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(h) : -1;
    esp_http_client_cleanup(h);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "control perform failed: %s", esp_err_to_name(err));
        return -1;
    }

    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "control HTTP %d target=%s action=%s", status, cmd->target, cmd->action);
        return status;
    }

    ESP_LOGI(TAG, "control ok target=%s action=%s", cmd->target, cmd->action);
    return status;
}

/* ── Small helper: read a number field from cJSON, with default ────── */
static float j_num(const cJSON *obj, const char *key, float def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : def;
}

static bool j_bool(const cJSON *obj, const char *key, bool def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(v)) {
        return cJSON_IsTrue(v);
    }
    return def;
}

/* ── Parse JSON and push values into the UI (under the LVGL lock) ──── */
static bool parse_and_apply(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        return false;
    }

    demo_data_t d = {
        .temp      = j_num(root, "outside_temp_c",  0.0f),
        .feel_temp = j_num(root, "feels_like_c",    0.0f),
        .wind_bft  = j_num(root, "wind_bft",        0.0f),
        .gust_bft  = j_num(root, "gust_bft",        0.0f),
        .humidity  = j_num(root, "humidity_pct",    0.0f),
        .pressure  = j_num(root, "pressure_hpa", 1013.0f),
        .wind_dir  = j_num(root, "wind_dir_deg",    0.0f),
    };
    bool night_mode = j_bool(root, "night_mode", false);

    /* Extract the pressure trend array */
    float   trend[MAX_BARO_POINTS];
    int     trend_count = 0;
    cJSON  *arr = cJSON_GetObjectItemCaseSensitive(root, "pressure_trend_24h");
    if (cJSON_IsArray(arr)) {
        int n = cJSON_GetArraySize(arr);
        if (n > MAX_BARO_POINTS) n = MAX_BARO_POINTS;
        for (int i = 0; i < n; i++) {
            cJSON *v = cJSON_GetArrayItem(arr, i);
            if (cJSON_IsNumber(v)) {
                trend[trend_count++] = (float)v->valuedouble;
            }
        }
    }

    /* Extract indoor zone data (before cJSON_Delete) */
    struct { float temp; float rh; bool temp_ok; bool rh_ok; } indoor[INDOOR_ZONES];
    int  indoor_count = 0;
    bool has_indoor   = false;

    cJSON *zones = cJSON_GetObjectItemCaseSensitive(root, "indoor_zones");
    if (cJSON_IsArray(zones)) {
        has_indoor = true;
        indoor_count = cJSON_GetArraySize(zones);
        if (indoor_count > INDOOR_ZONES) indoor_count = INDOOR_ZONES;
        for (int i = 0; i < indoor_count; i++) {
            cJSON *z  = cJSON_GetArrayItem(zones, i);
            cJSON *tc = cJSON_IsObject(z) ?
                        cJSON_GetObjectItemCaseSensitive(z, "temp_c") : NULL;
            cJSON *rh = cJSON_IsObject(z) ?
                        cJSON_GetObjectItemCaseSensitive(z, "rh_pct") : NULL;
            indoor[i].temp_ok = cJSON_IsNumber(tc);
            indoor[i].rh_ok   = cJSON_IsNumber(rh);
            indoor[i].temp    = indoor[i].temp_ok ? (float)tc->valuedouble : 0;
            indoor[i].rh      = indoor[i].rh_ok   ? (float)rh->valuedouble : 0;
        }
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG,
        "data: temp=%.1f feels=%.1f wind=%.0fbft gust=%.0fbft "
        "dir=%.0f hum=%.0f%% p=%.0fhPa trend=%dpts indoor=%d",
        (double)d.temp, (double)d.feel_temp,
        (double)d.wind_bft, (double)d.gust_bft,
        (double)d.wind_dir, (double)d.humidity,
        (double)d.pressure, trend_count, indoor_count);

    /* Push to UI under the LVGL lock. */
    _lock_acquire(&lvgl_api_lock);
    ui_weather_update(&d);
    if (trend_count > 0) {
        ui_weather_set_baro_trend(trend, trend_count);
    }
    ui_compass_set_direction(d.wind_dir);
    ui_weather_set_storm_active(d.gust_bft >= STORM_ALERT_GUST_BFT);
    ui_wind_strip_update(d.wind_bft, d.gust_bft);
    if (ui_theme_is_night_mode() != night_mode) {
        ui_theme_set_night_mode(night_mode);
        ui_theme_apply();
    }

    /* Indoor zones — only touch when the API includes the array.
     * If absent, the display keeps its current state (boot placeholders
     * or last-known values). */
    if (has_indoor) {
        for (int i = 0; i < indoor_count; i++) {
            ui_indoor_update(i, indoor[i].temp, indoor[i].rh,
                             indoor[i].temp_ok, indoor[i].rh_ok);
        }
        for (int i = indoor_count; i < INDOOR_ZONES; i++) {
            ui_indoor_update(i, 0, 0, false, false);
        }

        /* Feed the room climate to the weather hero / secondary on page 3.
         * Keuken (zone 4) for now; will come from config later. */
#define HERO_ZONE  4   /* Keuken — shown as hero on the control page */
        if (HERO_ZONE < indoor_count)
            ui_weather_set_room_climate(
                indoor[HERO_ZONE].temp, indoor[HERO_ZONE].temp_ok,
                indoor[HERO_ZONE].rh,   indoor[HERO_ZONE].rh_ok);
        else
            ui_weather_set_room_climate(0, false, 0, false);
    }
    _lock_release(&lvgl_api_lock);

    return true;
}

/* ── Main fetch loop ────────────────────────────────────────────────── */
static void panel_api_task(void *arg)
{
    (void)arg;
    char mac[PANEL_MAC_MAX] = "";
    char ip[PANEL_IP_MAX] = "";
    ESP_LOGI(TAG, "task started -> %s:%d%s",
             BUC_HOST, BUC_PORT, BUC_API_END_POINT);

    for (;;) {
        get_panel_identity(mac, sizeof(mac), ip, sizeof(ip));
        _lock_acquire(&lvgl_api_lock);
        ui_controls_set_debug_identity(ip, mac);
        _lock_release(&lvgl_api_lock);

        /* Wait for WiFi + IP before even trying a fetch. */
        if (net_wifi_get_status() != NET_WIFI_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
            continue;
        }

        if (fetch_panel_config_once() == 200) {
            (void)parse_panel_config(s_resp);
        }

        int status = fetch_once();
        bool ok = (status == 200) && parse_and_apply(s_resp);

        vTaskDelay(pdMS_TO_TICKS(ok ? FETCH_INTERVAL_MS : RETRY_MS));
    }
}

static void panel_control_task(void *arg)
{
    panel_control_command_t cmd;

    (void)arg;
    ESP_LOGI(TAG, "control task started");

    for (;;) {
        if (xQueueReceive(s_control_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (net_wifi_get_status() != NET_WIFI_CONNECTED) {
            ESP_LOGW(TAG, "drop control while WiFi disconnected target=%s action=%s",
                     cmd.target, cmd.action);
            continue;
        }

        (void)post_control_once(&cmd);
    }
}

/* ── Public API ────────────────────────────────────────────────────── */
void panel_api_init(void)
{
    if (s_started) return;
    s_started = true;

    init_default_profile();

    s_control_queue = xQueueCreate(CONTROL_QUEUE_LEN, sizeof(panel_control_command_t));
    if (s_control_queue == NULL) {
        ESP_LOGE(TAG, "failed to create control queue");
        return;
    }

    /* 8 KB stack: esp_http_client + cJSON parse fit with margin. */
    xTaskCreate(panel_api_task, "panel_api", 8 * 1024, NULL, 4, NULL);
    xTaskCreate(panel_control_task, "panel_ctl", 6 * 1024, NULL, 4, NULL);
}

void panel_api_send_control_command(const char *target, const char *action)
{
    panel_control_command_t cmd = {0};

    if (s_control_queue == NULL || target == NULL || action == NULL) {
        return;
    }

    snprintf(cmd.target, sizeof(cmd.target), "%s", target);
    snprintf(cmd.action, sizeof(cmd.action), "%s", action);

    if (xQueueSend(s_control_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "drop control command queue full target=%s action=%s", cmd.target, cmd.action);
    }
}

void panel_api_send_scene_command(int index, int long_press)
{
    if (index < 0 || index >= PANEL_SCENE_SLOTS) {
        return;
    }

    if (long_press) {
        if (s_profile_cfg.long_press.target[0] != '\0' && s_profile_cfg.long_press.action[0] != '\0') {
            panel_api_send_control_command(s_profile_cfg.long_press.target, s_profile_cfg.long_press.action);
        }
        return;
    }

    if (!s_profile_cfg.scenes[index].enabled) {
        return;
    }

    panel_api_send_control_command(s_profile_cfg.scenes[index].target, s_profile_cfg.scenes[index].action);
}

void panel_api_send_target_command(int index)
{
    if (index < 0 || index >= PANEL_TARGET_SLOTS) {
        return;
    }
    if (!s_profile_cfg.targets[index].enabled) {
        return;
    }

    panel_api_send_control_command(s_profile_cfg.targets[index].target, s_profile_cfg.targets[index].action);
}
