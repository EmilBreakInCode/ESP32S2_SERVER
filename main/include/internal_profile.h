// internal_profile.h
#pragma once

// ВКЛЮЧИТЬ внутреннее устройство
#define INTDEV_ENABLE           1

// Идентификатор (виден бэкенду и приложению)
#define INTDEV_DEVICE_ID        "internal-temp-sensor-01"
#define INTDEV_FW_VERSION       "internal-temp-1.0.0"

// Датчик: DS18B20 на GPIO16
#define INTDEV_USE_DS18B20      1
#define INTDEV_ONEWIRE_GPIO     17

// Периодики публикаций
#define INTDEV_STATE_PERIOD_MS  5000
#define INTDEV_TARGET_PERIOD_MS 10000
