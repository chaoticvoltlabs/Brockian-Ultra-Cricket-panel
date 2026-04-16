#pragma once

/**
 * Non-blocking dispatcher that POSTs panel control commands to BUC-server
 * from its own FreeRTOS task.  LVGL event callbacks enqueue commands with
 * control_post_enqueue() and return immediately; the dispatch task performs
 * the HTTP call so the display thread never blocks on the network.
 */
void control_post_init(void);

/**
 * Set the X-Panel-MAC / X-Panel-IP headers attached to every POST.
 * Safe to call before or after control_post_init().
 */
void control_post_set_identity(const char *mac_header, const char *ip_header);

/**
 * Queue a {target, action} pair for POST /api/panel/control.  Non-blocking.
 * If the queue is full the oldest entry is dropped.
 */
void control_post_enqueue(const char *target, const char *action);

/**
 * Queue a control command with a numeric value payload.
 */
void control_post_enqueue_value(const char *target, const char *action, int value);

/**
 * Queue a control command with an RGB payload.
 */
void control_post_enqueue_rgb(const char *target, const char *action, int r, int g, int b);
