// mqtt_manager.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mqtt_set_handler_t)(const char *data, int len);
/**
 * Функция должна вернуть длину записанного JSON (<= bufsize).
 * Верни 0, если публиковать сейчас нечего.
 */
typedef size_t (*mqtt_state_provider_t)(char *buf, size_t bufsize);

/** Запуск MQTT-клиента. Все строки — \0-terminated. */
esp_err_t mqtt_mgr_start(const char *broker_uri,
                         const char *username,
                         const char *password,
                         const char *user_login,
                         const char *server_id);

/** Остановить MQTT клиента. */
void mqtt_mgr_stop(void);

/** Установить обработчик входящих таргетов из .../target. */
void mqtt_mgr_set_handler(mqtt_set_handler_t handler);

/** Установить провайдер JSON состояния для публикации в .../state. */
void mqtt_mgr_set_state_provider(mqtt_state_provider_t provider);

/** Принудительно опубликовать состояние (если подключены). */
esp_err_t mqtt_mgr_publish_now(void);

/** Подключены ли к брокеру. */
bool mqtt_mgr_is_connected(void);

#ifdef __cplusplus
}
#endif

esp_err_t mqtt_mgr_publish_state_payload(const char *data, size_t len);
esp_err_t mqtt_mgr_publish_target_payload(const char *data, size_t len);