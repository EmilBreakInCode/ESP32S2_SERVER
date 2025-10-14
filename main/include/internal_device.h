// internal_device.h
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if __has_include("internal_profile.h")
#  include "internal_profile.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ВКЛ/ВЫКЛ «внутреннего устройства» компоновкой.
 * По умолчанию выключено, пока не создашь internal_profile.h (см. ниже).
 *
 * Если не хочешь Kconfig — просто создай internal_profile.h и выставь INTDEV_ENABLE 1.
 */
#ifndef INTDEV_ENABLE
#define INTDEV_ENABLE 0
#endif

/** Идентификатор устройства (как у внешних устройств) */
#ifndef INTDEV_DEVICE_ID
#define INTDEV_DEVICE_ID "int-temp-01"
#endif

/** Версия FW «внутреннего устройства» */
#ifndef INTDEV_FW_VERSION
#define INTDEV_FW_VERSION "intdev-1.0.0"
#endif

/** Периодики публикаций */
#ifndef INTDEV_STATE_PERIOD_MS
#define INTDEV_STATE_PERIOD_MS 3000
#endif
#ifndef INTDEV_TARGET_PERIOD_MS
#define INTDEV_TARGET_PERIOD_MS 10000
#endif

/** Вариант датчика: DS18B20 по 1-Wire на INTDEV_ONEWIRE_GPIO */
#ifndef INTDEV_USE_DS18B20
#define INTDEV_USE_DS18B20 0
#endif
#ifndef INTDEV_ONEWIRE_GPIO
#define INTDEV_ONEWIRE_GPIO 16
#endif

/* ===== Публичное API ===== */
void internal_dev_init(void);                    // безопасно звать всегда, даже если отключено
void internal_dev_start(void);                   // поднимает задачу измерений/публикаций
bool internal_dev_is_enabled(void);              // true, если включено компоновкой
const char* internal_dev_id(void);               // deviceId

/* Принять входящий /target из MQTT (если наше deviceId) */
void internal_dev_on_mqtt_target(const char *json, size_t len);

/* Разовая публикация (state + target snapshot), если нужно из вне */
void internal_dev_publish_now(void);

#ifdef __cplusplus
}
#endif
