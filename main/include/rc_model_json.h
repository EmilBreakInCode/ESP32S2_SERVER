#pragma once
/*
 * RostClimat — JSON адаптер для моделей (ESP32 Hub/Device).
 *
 * Назначение:
 *  - Сериализация/десериализация STATE и TARGET в MQTT/HTTP payload'ы.
 *  - Совместимость с Android/Backend DTO/Entity (camelCase).
 *  - Частичные апдейты через presenceFlags.
 *
 * Важно:
 *  - Прошивка НЕ отправляет updatedAt, isAlarm, alarmReason.
 *  - В анонсах/heartbeat присутствует lastHeartbeat (uint32_t).
 *  - Все числа печатаются с округлением до 2 знаков (реализация в .c).
 *  - STATE и TARGET РАЗДЕЛЕНЫ: отдельные encode/decode.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "rc_model.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ Ошибки ============================ */
typedef enum {
    RC_JSON_OK = 0,
    RC_JSON_ERR_INVALID_ARG,
    RC_JSON_ERR_TYPE_MISMATCH,
    RC_JSON_ERR_NO_MEMORY,
    RC_JSON_ERR_BUFFER_TOO_SMALL,
    RC_JSON_ERR_UNKNOWN_FIELD,
    RC_JSON_ERR_PARSE,
    RC_JSON_ERR_OVERFLOW
} rc_json_err_t;

/* ============================ Политики ============================ */
typedef struct {
    bool strictKeys;     /* неизвестные ключи → ошибка (decode), если true */
    bool stableKeyOrder; /* фиксированный порядок ключей при encode */
    bool acceptNulls;    /* null трактовать как «поля нет», если false */
    uint8_t _reserved;
} rc_json_policy_t;

extern const rc_json_policy_t RC_JSON_POLICY_DEFAULT;

/* ============================ Ключи (STATE) ============================ */
#define RCJ_KEY_deviceId           "deviceId"
#define RCJ_KEY_isOn               "isOn"
#define RCJ_KEY_temp               "temp"
#define RCJ_KEY_hum                "hum"
#define RCJ_KEY_isVentAuto         "isVentAuto"
#define RCJ_KEY_co2Ppm             "co2Ppm"
#define RCJ_KEY_ventilation        "ventilation"
#define RCJ_KEY_isOpen             "isOpen"
#define RCJ_KEY_distance           "distance"
#define RCJ_KEY_batteryLevel       "batteryLevel"
#define RCJ_KEY_voltage            "voltage"
#define RCJ_KEY_signalStrength     "signalStrength"
#define RCJ_KEY_firmwareVersion    "firmwareVersion"
#define RCJ_KEY_pm2_5              "pm2_5"
#define RCJ_KEY_pm10               "pm10"
#define RCJ_KEY_tvoc               "tvoc"
#define RCJ_KEY_pressure           "pressure"
#define RCJ_KEY_ambientLight       "ambientLight"
#define RCJ_KEY_noiseLevel         "noiseLevel"
#define RCJ_KEY_motionDetected     "motionDetected"
#define RCJ_KEY_smokeDetected      "smokeDetected"
#define RCJ_KEY_leakDetected       "leakDetected"
#define RCJ_KEY_mode               "mode"
#define RCJ_KEY_errorCode          "errorCode"
#define RCJ_KEY_statusMessage      "statusMessage"

/* ============================ Ключи (TARGET) ============================ */
/* VALUE */
#define RCJ_KEY_targetTemp            "targetTemp"
#define RCJ_KEY_targetHum             "targetHum"
#define RCJ_KEY_targetCo2Ppm          "targetCo2Ppm"
#define RCJ_KEY_targetPressure        "targetPressure"
#define RCJ_KEY_targetAmbientLight    "targetAmbientLight"
#define RCJ_KEY_targetVoltage         "targetVoltage"
#define RCJ_KEY_targetVentilation     "targetVentilation"
#define RCJ_KEY_targetDistance        "targetDistance"
#define RCJ_KEY_targetSpeedKmH        "targetSpeedKmH"
#define RCJ_KEY_targetSpeedMS         "targetSpeedMS"
#define RCJ_KEY_targetPosX            "targetPosX"
#define RCJ_KEY_targetPosY            "targetPosY"
#define RCJ_KEY_targetPosZ            "targetPosZ"
#define RCJ_KEY_targetIsOn            "targetIsOn"
#define RCJ_KEY_targetIsOpen          "targetIsOpen"
#define RCJ_KEY_targetIsVentAuto      "targetIsVentAuto"
#define RCJ_KEY_targetMode            "targetMode"

/* LIMITS: Min/Max/Step */
#define RCJ_KEY_targetTempMin         "targetTempMin"
#define RCJ_KEY_targetTempMax         "targetTempMax"
#define RCJ_KEY_targetTempStep        "targetTempStep"

#define RCJ_KEY_targetHumMin          "targetHumMin"
#define RCJ_KEY_targetHumMax          "targetHumMax"
#define RCJ_KEY_targetHumStep         "targetHumStep"

#define RCJ_KEY_targetCo2PpmMin       "targetCo2PpmMin"
#define RCJ_KEY_targetCo2PpmMax       "targetCo2PpmMax"
#define RCJ_KEY_targetCo2PpmStep      "targetCo2PpmStep"

#define RCJ_KEY_targetPressureMin     "targetPressureMin"
#define RCJ_KEY_targetPressureMax     "targetPressureMax"
#define RCJ_KEY_targetPressureStep    "targetPressureStep"

#define RCJ_KEY_targetAmbientLightMin  "targetAmbientLightMin"
#define RCJ_KEY_targetAmbientLightMax  "targetAmbientLightMax"
#define RCJ_KEY_targetAmbientLightStep "targetAmbientLightStep"

#define RCJ_KEY_targetVoltageMin      "targetVoltageMin"
#define RCJ_KEY_targetVoltageMax      "targetVoltageMax"
#define RCJ_KEY_targetVoltageStep     "targetVoltageStep"

#define RCJ_KEY_targetVentilationMin  "targetVentilationMin"
#define RCJ_KEY_targetVentilationMax  "targetVentilationMax"
#define RCJ_KEY_targetVentilationStep "targetVentilationStep"

#define RCJ_KEY_targetDistanceMin     "targetDistanceMin"
#define RCJ_KEY_targetDistanceMax     "targetDistanceMax"
#define RCJ_KEY_targetDistanceStep    "targetDistanceStep"

#define RCJ_KEY_targetSpeedKmHMin     "targetSpeedKmHMin"
#define RCJ_KEY_targetSpeedKmHMax     "targetSpeedKmHMax"
#define RCJ_KEY_targetSpeedKmHStep    "targetSpeedKmHStep"

#define RCJ_KEY_targetSpeedMSMin      "targetSpeedMSMin"
#define RCJ_KEY_targetSpeedMSMax      "targetSpeedMSMax"
#define RCJ_KEY_targetSpeedMSStep     "targetSpeedMSStep"

#define RCJ_KEY_targetPosXMin         "targetPosXMin"
#define RCJ_KEY_targetPosXMax         "targetPosXMax"
#define RCJ_KEY_targetPosXStep        "targetPosXStep"

#define RCJ_KEY_targetPosYMin         "targetPosYMin"
#define RCJ_KEY_targetPosYMax         "targetPosYMax"
#define RCJ_KEY_targetPosYStep        "targetPosYStep"

#define RCJ_KEY_targetPosZMin         "targetPosZMin"
#define RCJ_KEY_targetPosZMax         "targetPosZMax"
#define RCJ_KEY_targetPosZStep        "targetPosZStep"

/* ============================ Общие ключи для анонсов ============================ */
#define RCJ_KEY_deviceName         "deviceName"
#define RCJ_KEY_type               "type"
#define RCJ_KEY_serverId           "serverId"
#define RCJ_KEY_serverName         "serverName"
#define RCJ_KEY_lastHeartbeat      "lastHeartbeat" /* uint32 от устройства/сервера */
#define RCJ_KEY_devices            "devices"       /* массив объектов */

/* ============================ Оценка буфера ============================ */
size_t rc_json_estimate_state        (const rc_device_state_t  *st, const rc_json_policy_t *policy);
size_t rc_json_estimate_target       (const rc_device_target_t *tg, const rc_json_policy_t *policy);
size_t rc_json_estimate_device       (const rc_device_t *dev,        const rc_json_policy_t *policy);
size_t rc_json_estimate_devices_array(const rc_device_t *arr, size_t count, const rc_json_policy_t *policy);
size_t rc_json_estimate_server_announce(const rc_server_t *srv, const rc_device_t *arr, size_t count, uint32_t lastHeartbeat, const rc_json_policy_t *policy);
size_t rc_json_estimate_heartbeat(uint32_t lastHeartbeat);

/* ============================ Encode ============================ */
rc_json_err_t rc_json_encode_state(
    const rc_device_state_t *st,
    char *outBuf, size_t outLen, size_t *written,
    const rc_json_policy_t *policy);

/* ТОЛЬКО таргеты и лимиты (для топика .../target) */
rc_json_err_t rc_json_encode_target(
    const rc_device_target_t *tg,
    char *outBuf, size_t outLen, size_t *written,
    const rc_json_policy_t *policy);

/* Устройство как элемент списка (для announce/devices): deviceId, deviceName, type. */
rc_json_err_t rc_json_encode_device_min(
    const rc_device_t *dev,
    char *outBuf, size_t outLen, size_t *written,
    const rc_json_policy_t *policy);

/* Массив устройств: [ {deviceId, ...}, ... ] */
rc_json_err_t rc_json_encode_devices_array(
    const rc_device_t *arr, size_t count,
    char *outBuf, size_t outLen, size_t *written,
    const rc_json_policy_t *policy);

/* Анонс сервера (retained): { serverId, serverName, lastHeartbeat, devices:[...] } */
rc_json_err_t rc_json_encode_server_announce(
    const rc_server_t *srv,
    const rc_device_t *devices, size_t count,
    uint32_t lastHeartbeat,
    char *outBuf, size_t outLen, size_t *written,
    const rc_json_policy_t *policy);

/* Heartbeat: { lastHeartbeat: <uint32> } */
rc_json_err_t rc_json_encode_heartbeat(
    uint32_t lastHeartbeat,
    char *outBuf, size_t outLen, size_t *written);

/* ============================ Decode ============================ */
/* STATE из JSON (факты; target* здесь игнорируются) */
rc_json_err_t rc_json_decode_state(
    const char *json, size_t len,
    rc_device_state_t *outState,
    const rc_json_policy_t *policy);

/* TARGET из JSON (значения и/или лимиты; можно частично) */
rc_json_err_t rc_json_decode_target(
    const char *json, size_t len,
    rc_device_target_t *outTarget,
    const rc_json_policy_t *policy);

/* Массив устройств (минимум) */
rc_json_err_t rc_json_decode_devices_array(
    const char *json, size_t len,
    rc_device_t *outArr, size_t maxCount, size_t *outCount,
    const rc_json_policy_t *policy);

/* Анонс сервера */
rc_json_err_t rc_json_decode_server_announce(
    const char *json, size_t len,
    rc_server_t *outSrv,
    rc_device_t *outDevices, size_t maxDevices, size_t *outCount,
    const rc_json_policy_t *policy);

/* Heartbeat */
rc_json_err_t rc_json_decode_heartbeat(
    const char *json, size_t len,
    uint32_t *outLastHeartbeat,
    const rc_json_policy_t *policy);

/* ============================ Атрибут удобства ============================ */
#if defined(__GNUC__) || defined(__clang__)
#define RCJ_ATTR_WARN_UNUSED __attribute__((warn_unused_result))
#else
#define RCJ_ATTR_WARN_UNUSED
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif
