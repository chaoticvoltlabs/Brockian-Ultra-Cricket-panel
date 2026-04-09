/**
 * @file  net_wifi.h
 * @brief Minimal WiFi STA with hardcoded credentials + basic reconnect.
 *
 * No provisioning, no setup UI. Credentials live in main/secrets.h
 * (git-ignored). Status can be polled from any task to drive UI
 * indicators.
 */

#ifndef NET_WIFI_H
#define NET_WIFI_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    NET_WIFI_DISCONNECTED = 0,   /* no association or no DHCP lease */
    NET_WIFI_CONNECTED    = 1,   /* associated AND IP address acquired */
} net_wifi_status_t;

/**
 * Initialise NVS-dependent WiFi stack, register event handlers,
 * and start connecting. Returns on success; kicks off connection in
 * the background. Must be called after nvs_flash_init().
 */
void net_wifi_init(void);

/**
 * Current connection status. Safe to call from any task.
 * Returns CONNECTED only once an IP has been obtained.
 */
net_wifi_status_t net_wifi_get_status(void);

/**
 * Copy the current STA MAC string into @p buf as "xx:xx:xx:xx:xx:xx".
 * Writes an empty string on failure.
 */
void net_wifi_get_mac_string(char *buf, size_t buf_len);

/**
 * Copy the current STA IPv4 address into @p buf as "a.b.c.d".
 * Writes an empty string if no lease is active yet.
 */
void net_wifi_get_ipv4_string(char *buf, size_t buf_len);

#endif /* NET_WIFI_H */
