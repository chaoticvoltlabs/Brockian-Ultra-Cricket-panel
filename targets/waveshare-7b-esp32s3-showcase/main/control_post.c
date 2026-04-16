#include "control_post.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"

#include "config.h"

static const char *TAG = "control_post";

#define CTRL_Q_DEPTH 8

typedef struct {
    char target[40];
    char action[16];
    bool has_value;
    int  value;
    bool has_rgb;
    int  rgb[3];
} ctrl_msg_t;

static QueueHandle_t s_queue = NULL;
static char s_mac_hdr[18] = "";
static char s_ip_hdr[16]  = "";

static void post_one(const ctrl_msg_t *msg)
{
    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d%s",
             BUC_HOST, BUC_PORT, BUC_API_CONTROL_ENDPOINT);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGW(TAG, "json alloc failed for %s:%s", msg->target, msg->action);
        return;
    }
    cJSON_AddStringToObject(root, "target", msg->target);
    cJSON_AddStringToObject(root, "action", msg->action);
    if (msg->has_value) {
        cJSON_AddNumberToObject(root, "value", msg->value);
    }
    if (msg->has_rgb) {
        cJSON *arr = cJSON_AddArrayToObject(root, "rgb");
        if (arr != NULL) {
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(msg->rgb[0]));
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(msg->rgb[1]));
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(msg->rgb[2]));
        }
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        ESP_LOGW(TAG, "json encode failed for %s:%s", msg->target, msg->action);
        return;
    }
    int body_len = (int)strlen(body);

    esp_http_client_config_t cfg = {
        .url        = url,
        .method     = HTTP_METHOD_POST,
        .timeout_ms = 4000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGW(TAG, "http init failed for %s:%s", msg->target, msg->action);
        cJSON_free(body);
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (s_mac_hdr[0] != '\0') {
        esp_http_client_set_header(client, "X-Panel-MAC", s_mac_hdr);
    }
    if (s_ip_hdr[0] != '\0') {
        esp_http_client_set_header(client, "X-Panel-IP", s_ip_hdr);
    }
    esp_http_client_set_post_field(client, body, body_len);

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);
    cJSON_free(body);

    ESP_LOGI(TAG, "POST %s:%s -> HTTP %d (%s)",
             msg->target, msg->action, status, esp_err_to_name(err));
}

static void control_post_task(void *arg)
{
    (void)arg;
    ctrl_msg_t msg;
    while (1) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) == pdTRUE) {
            post_one(&msg);
        }
    }
}

void control_post_init(void)
{
    if (s_queue != NULL) {
        return;
    }

    s_queue = xQueueCreate(CTRL_Q_DEPTH, sizeof(ctrl_msg_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "failed to create queue");
        return;
    }

    xTaskCreatePinnedToCore(control_post_task, "ctrl_post",
                            8 * 1024, NULL, 2, NULL, 0);
    ESP_LOGI(TAG, "dispatcher started");
}

void control_post_set_identity(const char *mac_header, const char *ip_header)
{
    if (mac_header != NULL) {
        strncpy(s_mac_hdr, mac_header, sizeof(s_mac_hdr) - 1);
        s_mac_hdr[sizeof(s_mac_hdr) - 1] = '\0';
    }
    if (ip_header != NULL) {
        strncpy(s_ip_hdr, ip_header, sizeof(s_ip_hdr) - 1);
        s_ip_hdr[sizeof(s_ip_hdr) - 1] = '\0';
    }
}

void control_post_enqueue(const char *target, const char *action)
{
    if (s_queue == NULL || target == NULL || action == NULL) {
        return;
    }

    ctrl_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.target, target, sizeof(msg.target) - 1);
    strncpy(msg.action, action, sizeof(msg.action) - 1);

    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        ctrl_msg_t discard;
        xQueueReceive(s_queue, &discard, 0);
        if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "queue send failed for %s:%s", target, action);
        } else {
            ESP_LOGW(TAG, "queue full, dropped oldest");
        }
    }
}

void control_post_enqueue_value(const char *target, const char *action, int value)
{
    if (s_queue == NULL || target == NULL || action == NULL) {
        return;
    }

    ctrl_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.target, target, sizeof(msg.target) - 1);
    strncpy(msg.action, action, sizeof(msg.action) - 1);
    msg.has_value = true;
    msg.value = value;

    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        ctrl_msg_t discard;
        xQueueReceive(s_queue, &discard, 0);
        (void)xQueueSend(s_queue, &msg, 0);
    }
}

void control_post_enqueue_rgb(const char *target, const char *action, int r, int g, int b)
{
    if (s_queue == NULL || target == NULL || action == NULL) {
        return;
    }

    ctrl_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.target, target, sizeof(msg.target) - 1);
    strncpy(msg.action, action, sizeof(msg.action) - 1);
    msg.has_rgb = true;
    msg.rgb[0] = r;
    msg.rgb[1] = g;
    msg.rgb[2] = b;

    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        ctrl_msg_t discard;
        xQueueReceive(s_queue, &discard, 0);
        (void)xQueueSend(s_queue, &msg, 0);
    }
}
