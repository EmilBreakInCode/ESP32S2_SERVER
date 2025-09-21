// mqtt_bridge.c
#include "mqtt_manager.h"
#include "esp_log.h"
#include "cJSON.h"
#include "espnow_manager.h"
#include <string.h>

static const char *TAG = "mqtt_bridge";


/* ===== обработчик входящих таргетов из .../target ===== */
static void on_mqtt_target(const char *data, int len)
{
    ESP_LOGI(TAG, "MQTT /target in: %.*s", len, data);
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) { ESP_LOGW(TAG, "bad JSON in /target"); return; }

    const cJSON *j_id = cJSON_GetObjectItem(root, "deviceId");
    const char *devId = (cJSON_IsString(j_id) && j_id->valuestring) ? j_id->valuestring : NULL;

    // round_numbers_2dec(root);  // ← убрать

    // гарантируем t:"set"
    cJSON *jt = cJSON_GetObjectItem(root, "t");
    if (!cJSON_IsString(jt)) {
        cJSON_AddStringToObject(root, "t", "set");
    } else if (strcmp(jt->valuestring, "set") != 0) {
        cJSON_ReplaceItemInObject(root, "t", cJSON_CreateString("set"));
    }

    // deviceId удаляем перед ESPNOW (адресуем по MAC)
    if (devId && devId[0]) {
        cJSON_DeleteItemFromObjectCaseSensitive(root, "deviceId");
    }

    char *clean = cJSON_PrintUnformatted(root);

    esp_err_t se = ESP_OK;
    if (!devId || !devId[0]) {
        ESP_LOGW(TAG, "target without deviceId → broadcast");
        se = espnow_mgr_send_set_json(NULL, clean ? clean : data, (size_t)(clean ? strlen(clean) : len));
    } else {
        ESP_LOGI(TAG, "forward target to deviceId=%s (ESP-NOW)", devId);
        se = espnow_mgr_send_set_json(devId, clean ? clean : data, (size_t)(clean ? strlen(clean) : len));
    }
    ESP_LOGI(TAG, "espnow_mgr_send_set_json rc=%d", (int)se);
    if (clean) cJSON_free(clean);
    cJSON_Delete(root);
}

/* ===== колбэки ESPNOW ===== */
static void on_hello(const char *deviceId, const uint8_t mac[6])
{
    ESP_LOGI(TAG, "HELLO deviceId=%s mac=%02X:%02X:%02X:%02X:%02X:%02X",
             deviceId, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}

static void on_state_json(const char *deviceId, const char *json, int len)
{
    ESP_LOGI(TAG, "STATE from %s len=%d", deviceId ? deviceId : "(null)", len);
    ESP_LOGD(TAG, "STATE JSON: %.*s", len, json);

    // НИЧЕГО НЕ ПАРСИМ — пробрасываем как есть в MQTT /state
    esp_err_t pe = mqtt_mgr_publish_state_payload(json, (size_t)len);
    if (pe != ESP_OK) {
        ESP_LOGW(TAG, "MQTT publish(state) failed: %d", (int)pe);
    }
}

static void on_target_json(const char *deviceId, const char *json, size_t len)
{
    ESP_LOGI(TAG, "decoded TARGET: deviceId=%s (raw forward)", deviceId ? deviceId : "(null)");

    // Пробрасываем как есть в отдельный топик /target
    esp_err_t pe = mqtt_mgr_publish_target_payload(json, len);

    if (pe != ESP_OK) {
        ESP_LOGW(TAG, "MQTT publish(target) failed: %d", (int)pe);
    } else {
        ESP_LOGD(TAG, "MQTT target published for %s, %u bytes", deviceId ? deviceId : "(null)", (unsigned)len);
    }
}

/* ===== публичные точки подключения ===== */
void mqtt_bridge_attach(void)
{
    mqtt_mgr_set_handler(on_mqtt_target);

    /* Отключаем периодический агрегат под /state (push-only per-device). */
    mqtt_mgr_set_state_provider(NULL);

    espnow_mgr_set_callbacks(on_hello, on_state_json, on_target_json);
}
