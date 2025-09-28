// espnow_manager.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RC_MAX_SATELLITES
#define RC_MAX_SATELLITES 16
#endif

typedef void (*espnow_on_state_json_cb_t)(const char *deviceId, const char *json, int len);
typedef void (*espnow_on_hello_cb_t)(const char *deviceId, const uint8_t mac[6]);
typedef void (*espnow_on_target_json_cb_t)(const char *deviceId, const char *json, size_t len);

/** Запуск ESPNOW. Работает совместно с Wi-Fi AP+STA (канал берётся у Wi-Fi). */
esp_err_t espnow_mgr_start(void);
void      espnow_mgr_stop(void);

/** Колбэки (можно вызывать до/после start). */
void espnow_mgr_set_callbacks(espnow_on_hello_cb_t on_hello,
                              espnow_on_state_json_cb_t on_state,
                              espnow_on_target_json_cb_t on_target);

/** (Диагностика) Глобально включить/выключить фоновые broadcast-запросы {"t":"get"}.
 *  По умолчанию ВЫКЛЮЧЕНО (push-only). interval_ms игнорируется при disable.
 */
esp_err_t espnow_mgr_set_polling(bool enable, uint32_t interval_ms);

/** Разовый широковещательный запрос состояния ({"t":"get"}). */
esp_err_t espnow_mgr_broadcast_get(void);

/** Запрос состояния конкретного deviceId (unicast если знаем MAC, иначе broadcast c deviceId). */
esp_err_t espnow_mgr_request_state(const char *deviceId);

/** Отправить JSON-команду "set" на deviceId (unicast или broadcast fallback). */
esp_err_t espnow_mgr_send_set_json(const char *deviceId, const char *json, size_t len);

/** Узнаём MAC по deviceId (если уже видели HELLO/state). */
bool    espnow_mgr_lookup_mac(const char *deviceId, uint8_t mac_out[6]);

/** Текущий Wi-Fi канал (для логов). */
uint8_t espnow_mgr_current_channel(void);

#ifdef __cplusplus
}
#endif
