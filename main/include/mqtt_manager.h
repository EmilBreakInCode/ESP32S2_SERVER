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
/** Провайдер состояния: вернуть длину записанного JSON (<= bufsize), либо 0 если нечего. */
typedef size_t (*mqtt_state_provider_t)(char *buf, size_t bufsize);

/** Запуск/останов MQTT-клиента */
esp_err_t mqtt_mgr_start(const char *broker_uri,
                         const char *username,
                         const char *password,
                         const char *user_login,
                         const char *server_id);
void mqtt_mgr_stop(void);

/** Обработчик входящих /target и провайдер периодического /state */
void mqtt_mgr_set_handler(mqtt_set_handler_t handler);
void mqtt_mgr_set_state_provider(mqtt_state_provider_t provider);

/** Разовая публикация состояния + проверка подключения */
esp_err_t mqtt_mgr_publish_now(void);
bool mqtt_mgr_is_connected(void);

/** Прямые публикации готовых payload’ов (без парсинга) */
esp_err_t mqtt_mgr_publish_state_payload(const char *data, size_t len);
esp_err_t mqtt_mgr_publish_target_payload(const char *data, size_t len);

#ifdef __cplusplus
}
#endif
