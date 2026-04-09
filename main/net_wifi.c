/**
 * @file  net_wifi.c
 * @brief WiFi STA with hardcoded credentials + basic reconnect.
 *
 * Scope (intentionally minimal):
 *   - Hardcoded SSID / password from secrets.h
 *   - Automatic reconnect on disconnect (fire-and-forget)
 *   - Atomic status flag for UI polling: CONNECTED is only true
 *     once an IP address has been acquired; any subsequent disconnect
 *     flips it back to DISCONNECTED immediately.
 *   - No provisioning, no scanning, no roaming.
 */

#include "net_wifi.h"
#include "secrets.h"

#include <string.h>
#include <stdatomic.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

static const char *TAG = "net_wifi";

/* Atomic so the LVGL UI-polling timer can read without a lock. */
static _Atomic net_wifi_status_t s_status = NET_WIFI_DISCONNECTED;
static char s_mac_str[18] = "";
static char s_ip_str[16] = "";

/* ── Event handler ─────────────────────────────────────────────────── */
static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA_START -> connecting to \"%s\"", WIFI_SSID);
        esp_wifi_connect();
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *ev = (wifi_event_sta_connected_t *)data;
        ESP_LOGI(TAG,
            "STA_CONNECTED: AP=%.32s bssid=%02x:%02x:%02x:%02x:%02x:%02x ch=%d",
            ev->ssid,
            ev->bssid[0], ev->bssid[1], ev->bssid[2],
            ev->bssid[3], ev->bssid[4], ev->bssid[5],
            ev->channel);
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
        atomic_store(&s_status, NET_WIFI_DISCONNECTED);
        s_ip_str[0] = '\0';
        ESP_LOGW(TAG, "STA_DISCONNECTED reason=%d -> reconnecting", ev->reason);
        esp_wifi_connect();
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "GOT_IP: ip=" IPSTR " gw=" IPSTR " mask=" IPSTR,
                 IP2STR(&ev->ip_info.ip),
                 IP2STR(&ev->ip_info.gw),
                 IP2STR(&ev->ip_info.netmask));
        atomic_store(&s_status, NET_WIFI_CONNECTED);
        return;
    }
}

/* ── Public API ────────────────────────────────────────────────────── */
void net_wifi_init(void)
{
    ESP_LOGI(TAG, "init: bringing up netif + event loop");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid,     WIFI_SSID,     sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wc.sta.threshold.rssi     = -80;            /* reject very weak APs    */
    wc.sta.scan_method        = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method        = WIFI_CONNECT_AP_BY_SIGNAL;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Log the STA MAC so the firewall / DHCP lease table can be matched
     * without having to read it off the chip some other way. Must be
     * after esp_wifi_start() -- before that the MAC isn't programmed. */
    uint8_t mac[6] = { 0 };
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(s_mac_str, sizeof(s_mac_str),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG, "STA MAC = %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    ESP_LOGI(TAG, "WiFi started, SSID=\"%s\" (waiting for GOT_IP)", WIFI_SSID);
}

net_wifi_status_t net_wifi_get_status(void)
{
    return atomic_load(&s_status);
}

void net_wifi_get_mac_string(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }
    snprintf(buf, buf_len, "%s", s_mac_str);
}

void net_wifi_get_ipv4_string(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }
    snprintf(buf, buf_len, "%s", s_ip_str);
}
