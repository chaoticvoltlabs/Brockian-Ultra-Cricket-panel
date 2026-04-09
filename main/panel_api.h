/**
 * @file  panel_api.h
 * @brief Live weather data client for the upstream BUC panel API.
 *
 * Polls a small JSON endpoint at a fixed interval and pushes the values
 * into the existing UI update functions. Replaces the demo_data random
 * walk once WiFi is up. Runs in its own FreeRTOS task because the
 * HTTP client is blocking and must not be called from the LVGL task.
 */

#ifndef PANEL_API_H
#define PANEL_API_H

/**
 * Start the background fetch task. Safe to call once after net_wifi_init.
 * The task waits for WiFi to become available before the first fetch
 * and reconnects automatically; it is a no-op if called more than once.
 */
void panel_api_init(void);

/**
 * Queue a compact control command for the upstream BUC control endpoint.
 * The command is dropped if the queue is full or WiFi is unavailable.
 */
void panel_api_send_control_command(const char *target, const char *action);

/** Dispatch the configured scene command for page 3. */
void panel_api_send_scene_command(int index, int long_press);

/** Dispatch the configured direct-control command for page 3. */
void panel_api_send_target_command(int index);

#endif /* PANEL_API_H */
