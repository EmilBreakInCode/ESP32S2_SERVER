// device_registry.h
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rc_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Инициализация реестра устройств на сервере. */
void registry_init(const char *serverId, const char *userLogin);

/** Обновить/создать устройство по deviceId и применить состояние (из rc_model_json). */
void registry_apply_state(const rc_device_state_t *st);

/** Собрать JSON для публикации в MQTT /state (возвращает длину, 0 — нечего). */
size_t registry_build_state_json(char *buf, size_t cap);

/** Хелпер: сколько устройств сейчас известно. */
size_t registry_device_count(void);

#ifdef __cplusplus
}
#endif
