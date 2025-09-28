// device_registry.h
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Инициализация реестра устройств на сервере (метаданные только для справки). */
void registry_init(const char *serverId, const char *userLogin);

/**
 * Сохранить/обновить «сырое» состояние устройства.
 * Мы НЕ парсим JSON: просто запоминаем deviceId и последний полученный raw-пакет (для диагностики/веба).
 */
void registry_apply_state(const char *deviceId, const char *raw_json, size_t raw_len);

/**
 * Собрать лёгкий JSON для MQTT /state или веб-диагностики БЕЗ вложенного парсинга полей устройств.
 * Формат: {"serverId":"...","devices":[{"deviceId":"..."}...]}
 * Возвращает длину. Если буфера не хватает — 0.
 */
size_t registry_build_state_json(char *buf, size_t cap);

/** Хелпер: сколько устройств сейчас известно. */
size_t registry_device_count(void);

#ifdef __cplusplus
}
#endif
