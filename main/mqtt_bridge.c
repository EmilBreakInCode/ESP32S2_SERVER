// mqtt_bridge.c
#include "mqtt_manager.h"
#include "esp_log.h"
#include "cJSON.h"
#include "espnow_manager.h"
#include "device_registry.h"
#include "rc_model_json.h"
#include <string.h>
#include <math.h>

static const char *TAG = "mqtt_bridge";

/* Рекурсивно округлить все числовые поля JSON до 2 знаков после запятой */
static void round_numbers_2dec(cJSON *n) {
    if (!n) return;
    for (cJSON *it = n->child; it; it = it->next) {
        if (cJSON_IsNumber(it)) {
            double r = round(it->valuedouble * 100.0) / 100.0;
            it->valuedouble = r;
        } else if (cJSON_IsObject(it) || cJSON_IsArray(it)) {
            round_numbers_2dec(it);
        }
    }
}

/* ===== обработчик входящих таргетов из .../target ===== */
static void on_mqtt_target(const char *data, int len)
{
    ESP_LOGI(TAG, "MQTT /target in: %.*s", len, data);
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) { ESP_LOGW(TAG, "bad JSON in /target"); return; }

    const cJSON *j_id = cJSON_GetObjectItem(root, "deviceId");
    const char *devId = (cJSON_IsString(j_id) && j_id->valuestring) ? j_id->valuestring : NULL;

    round_numbers_2dec(root);

    // гарантируем t:"set"
    cJSON *jt = cJSON_GetObjectItem(root, "t");
    if (!cJSON_IsString(jt)) {
        cJSON_AddStringToObject(root, "t", "set");
    } else if (strcmp(jt->valuestring, "set") != 0) {
        // заменить любое другое значение на "set"
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

/* ===== helper: собрать плоский JSON { "deviceId": "...", <поля состояния> } ===== */
static size_t build_flat_state_json(char *out, size_t cap, const rc_device_state_t *st)
{
    if (!out || cap < 64 || !st) return 0;

    /* сначала кодируем только состояние */
    char tmp[1024];
    size_t n = 0;
    rc_json_err_t r = rc_json_encode_state(st, tmp, sizeof(tmp), &n, &RC_JSON_POLICY_DEFAULT);
    if (r != RC_JSON_OK || n < 2 || tmp[0] != '{' || tmp[n-1] != '}') {
        ESP_LOGW(TAG, "encode_state failed (%d) or invalid braces", (int)r);
        return 0;
    }

    /* tmp = { ... } → вставим deviceId в корень */
    // итог: {"deviceId":"XYZ",<содержимое tmp без внешних скобок>}
    int written = snprintf(out, cap, "{\"deviceId\":\"%s\",%.*s}", st->deviceId, (int)(n - 2), tmp + 1);
    if (written <= 0 || (size_t)written >= cap) return 0;
    return (size_t)written;
}


/* ===== колбэки ESPNOW ===== */
static void on_hello(const char *deviceId, const uint8_t mac[6])
{
    ESP_LOGI(TAG, "HELLO deviceId=%s mac=%02X:%02X:%02X:%02X:%02X:%02X",
             deviceId, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}

static void on_state_json(const char *deviceId, const char *json, int len)
{
    rc_device_state_t st;
    rc_device_state_clear(&st);
    strncpy(st.deviceId, deviceId, sizeof(st.deviceId)-1);

    rc_json_err_t r = rc_json_decode_state(json, (size_t)len, &st, &RC_JSON_POLICY_DEFAULT);
    if (r == RC_JSON_OK) {
        ESP_LOGI(TAG, "decoded STATE: deviceId=%s (ok)", st.deviceId);

        /* локальный реестр для веб/диагностики */
        registry_apply_state(&st);

        /* собираем плоский JSON и публикуем в MQTT /state */
        char out[1200];
        size_t wn = build_flat_state_json(out, sizeof(out), &st);
        if (wn > 0) {
            esp_err_t pe = mqtt_mgr_publish_state_payload(out, wn); // ← ВАЖНО: новая функция
            if (pe != ESP_OK) {
                ESP_LOGW(TAG, "MQTT publish(state) failed: %d", (int)pe);
            } else {
                ESP_LOGD(TAG, "MQTT state published for %s, %u bytes", st.deviceId, (unsigned)wn);
            }
        } else {
            ESP_LOGW(TAG, "Failed to build flat state JSON for %s", st.deviceId);
        }
    } else {
        ESP_LOGW(TAG, "bad state JSON from %s (err=%d)", deviceId, (int)r);
    }
}

static void on_target_json(const char *deviceId, const char *json, size_t len)
{
    ESP_LOGI(TAG, "decoded TARGET: deviceId=%s (raw forward)", deviceId ? deviceId : "(null)");

    // Вариант А: отдельный топик /target (предпочтительно)
    // esp_err_t pe = mqtt_mgr_publish_target_payload(json, len);

    // Вариант Б: публикуем как есть через общий паблишер (если он один)
    // Консьюмер сможет различить по "t":"target"
    esp_err_t pe = mqtt_mgr_publish_target_payload(json, len);

    if (pe != ESP_OK) {
        ESP_LOGW(TAG, "MQTT publish(target) failed: %d", (int)pe);
    } else {
        ESP_LOGD(TAG, "MQTT target published for %s, %u bytes", deviceId, (unsigned)len);
    }
}

/* ===== публичные точки подключения ===== */
void mqtt_bridge_attach(void)
{
    mqtt_mgr_set_handler(on_mqtt_target);

    /* ВАЖНО: отключаем периодический агрегат под /state,
       иначе бэкенд снова увидит «devices[]». Мы шлём плоские per-device /state. */
    mqtt_mgr_set_state_provider(NULL);

    espnow_mgr_set_callbacks(on_hello, on_state_json, on_target_json);
}
