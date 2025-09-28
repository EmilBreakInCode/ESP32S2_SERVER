// mqtt_manager.c
#include "mqtt_manager.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mqtt_mgr";

#define TOPIC_MAX 160
#define STATE_BUF (8*1024)   // буфер под JSON состояния

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

static char s_user_login[64];
static char s_server_id[64];

static char s_topic_target[TOPIC_MAX];
static char s_topic_state[TOPIC_MAX];

static mqtt_set_handler_t     s_on_set = NULL;
static mqtt_state_provider_t  s_state_provider = NULL;

static TaskHandle_t s_pub_task = NULL;

/* ---------- внутренние ---------- */

static void build_topics(void) {
    snprintf(s_topic_target, sizeof(s_topic_target), "rostclimat/%s/%s/target", s_user_login, s_server_id);
    snprintf(s_topic_state,  sizeof(s_topic_state),  "rostclimat/%s/%s/state",  s_user_login, s_server_id);
    ESP_LOGI(TAG, "Topics: target='%s', state='%s'", s_topic_target, s_topic_state);
}

static void mqtt_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args; (void)base;
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "Connected to broker");
        esp_mqtt_client_subscribe(s_client, s_topic_target, 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected from broker");
        break;

    case MQTT_EVENT_DATA:
        if (e->topic && e->topic_len) {
        if ((size_t)e->topic_len == strlen(s_topic_target) &&
            memcmp(e->topic, s_topic_target, e->topic_len) == 0)
        {
            // Если это наш же снапшот от устройства (с лимитами) — игнорируем
            const char *p = (const char*)e->data;
            if (e->data && e->data_len > 0) {
                for (int i = 0; i + 7 < e->data_len; ++i) {
                    if (p[i] == '"' && strncmp(&p[i], "\"limits\"", 8) == 0) {
                        ESP_LOGD(TAG, "Skip device snapshot on /target (contains \"limits\")");
                        return;
                    }
                }
            }
            ESP_LOGI(TAG, "TARGET message %.*s", e->data_len, e->data);
            if (s_on_set) s_on_set(e->data, e->data_len);
        }
    }
    break;

    default:
        break;
    }
}

static void pub_task(void *arg)
{
    (void)arg;
    static char *buf = NULL;
    buf = (char*)malloc(STATE_BUF);
    if (!buf) {
        ESP_LOGE(TAG, "No mem for state buffer");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        if (s_connected && s_state_provider) {
            size_t n = s_state_provider(buf, STATE_BUF);
            if (n > 0 && n < STATE_BUF) {
                int mid = esp_mqtt_client_publish(s_client, s_topic_state, buf, (int)n, 1, false);
                if (mid >= 0) {
                    ESP_LOGD(TAG, "Published state (%d bytes), mid=%d", (int)n, mid);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 Гц по умолчанию
    }
}

/* ---------- публичное API ---------- */

esp_err_t mqtt_mgr_start(const char *broker_uri,
                         const char *username,
                         const char *password,
                         const char *user_login,
                         const char *server_id)
{
    if (s_client) return ESP_OK;
    if (!broker_uri || !user_login || !server_id) return ESP_ERR_INVALID_ARG;

    strlcpy(s_user_login, user_login, sizeof(s_user_login));
    strlcpy(s_server_id,  server_id,  sizeof(s_server_id));
    build_topics();

    esp_mqtt_client_config_t cfg = {
        .broker = {
            .address.uri = broker_uri,
            .verification = {
                .crt_bundle_attach = esp_crt_bundle_attach, // TLS trust bundle
            },
        },
        .credentials = {
            .username = username,
            .authentication.password = password,
        },
        .network = {
            .disable_auto_reconnect = false,
            .reconnect_timeout_ms = 3000,
        },
        .session = {
            .keepalive = 30,
        },
        .task = {
            .priority = 5,
        },
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) return ESP_FAIL;

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) return err;

    if (!s_pub_task) {
        xTaskCreate(pub_task, "mqtt_pub", 4096, NULL, 4, &s_pub_task);
    }
    return ESP_OK;
}

void mqtt_mgr_stop(void)
{
    if (s_pub_task) {
        vTaskDelete(s_pub_task);
        s_pub_task = NULL;
    }
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
}

void mqtt_mgr_set_handler(mqtt_set_handler_t handler)
{
    s_on_set = handler;
}

void mqtt_mgr_set_state_provider(mqtt_state_provider_t provider)
{
    s_state_provider = provider;
}

esp_err_t mqtt_mgr_publish_now(void)
{
    if (!s_connected || !s_state_provider) return ESP_ERR_INVALID_STATE;
    static char *buf = NULL;
    if (!buf) buf = (char*)malloc(STATE_BUF);
    if (!buf) return ESP_ERR_NO_MEM;
    size_t n = s_state_provider(buf, STATE_BUF);
    if (n == 0 || n >= STATE_BUF) return ESP_ERR_INVALID_SIZE;
    int mid = esp_mqtt_client_publish(s_client, s_topic_state, buf, (int)n, 1, false);
    return (mid >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_mgr_publish_state_payload(const char *data, size_t len)
{
    if (!s_connected || !s_client || !data || len == 0) return ESP_ERR_INVALID_STATE;
    int mid = esp_mqtt_client_publish(s_client, s_topic_state, data, (int)len, 1, false);
    if (mid >= 0) {
        ESP_LOGD(TAG, "Published raw state (%u bytes), mid=%d", (unsigned)len, mid);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t mqtt_mgr_publish_target_payload(const char *data, size_t len)
{
    if (!s_connected || !s_client || !data || len == 0) return ESP_ERR_INVALID_STATE;
    int mid = esp_mqtt_client_publish(s_client, s_topic_target, data, (int)len, 1, false);
    if (mid >= 0) {
        ESP_LOGD(TAG, "Published raw target (%u bytes), mid=%d", (unsigned)len, mid);
        return ESP_OK;
    }
    return ESP_FAIL;
}

bool mqtt_mgr_is_connected(void)
{
    return s_connected;
}
