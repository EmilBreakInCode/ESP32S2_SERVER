#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====== Размеры строк (включая '\0') ====== */
#define RC_ID_LEN       64   /* serverId, deviceId */
#define RC_NAME_LEN     64   /* serverName, deviceName */
#define RC_TYPE_LEN     32   /* type */
#define RC_FW_LEN       32   /* firmwareVersion */
#define RC_MODE_LEN     24   /* mode / targetMode */
#define RC_STATUS_LEN  128   /* statusMessage */

/* ====== Общие константы ====== */
#define RC_ID_NONE         (-1)   /* неизвестный БД id */
#define RC_EPOCH_UNKNOWN   0ULL   /* резерв */

//
// ──────────────────────────────────────────────────────────────────────────────
//   ФАКТИЧЕСКОЕ СОСТОЯНИЕ (STATE)
// ──────────────────────────────────────────────────────────────────────────────
//

/* Флаги присутствия опциональных полей в rc_device_state_t */
typedef enum {
    RC_DSF_TEMP                = (1u << 0),
    RC_DSF_HUM                 = (1u << 1),
    RC_DSF_IS_VENT_AUTO        = (1u << 2),
    RC_DSF_CO2_PPM             = (1u << 3),
    RC_DSF_VENTILATION         = (1u << 4),
    RC_DSF_IS_OPEN             = (1u << 5),
    RC_DSF_DISTANCE            = (1u << 6),
    RC_DSF_BATTERY_LEVEL       = (1u << 7),
    RC_DSF_VOLTAGE             = (1u << 8),
    RC_DSF_SIGNAL_STRENGTH     = (1u << 9),
    RC_DSF_FIRMWARE_VERSION    = (1u << 10),
    RC_DSF_PM2_5               = (1u << 11),
    RC_DSF_PM10                = (1u << 12),
    RC_DSF_TVOC                = (1u << 13),
    RC_DSF_PRESSURE            = (1u << 14),
    RC_DSF_AMBIENT_LIGHT       = (1u << 15),
    RC_DSF_NOISE_LEVEL         = (1u << 16),
    RC_DSF_MOTION_DETECTED     = (1u << 17),
    RC_DSF_SMOKE_DETECTED      = (1u << 18),
    RC_DSF_LEAK_DETECTED       = (1u << 19),
    RC_DSF_MODE                = (1u << 20),
    RC_DSF_ERROR_CODE          = (1u << 21),
    RC_DSF_STATUS_MESSAGE      = (1u << 22),
} rc_device_state_flags_t;

/* Фактическое состояние устройства (без target*) */
typedef struct {
    int64_t  id;                         /* PK БД, если известен; иначе RC_ID_NONE */
    char     deviceId[RC_ID_LEN];        /* например "plc-0" */
    bool     isOn;                       /* обязательное */

    /* Сенсорика/факты (опциональные, см. presenceFlags) */
    float    temp;                       /* RC_DSF_TEMP */
    float    hum;                        /* RC_DSF_HUM */
    bool     isVentAuto;                 /* RC_DSF_IS_VENT_AUTO */
    float    co2Ppm;                     /* RC_DSF_CO2_PPM */
    float    ventilation;                /* RC_DSF_VENTILATION */
    bool     isOpen;                     /* RC_DSF_IS_OPEN */
    float    distance;                   /* RC_DSF_DISTANCE */
    float    batteryLevel;               /* RC_DSF_BATTERY_LEVEL */
    float    voltage;                    /* RC_DSF_VOLTAGE */
    float    signalStrength;             /* RC_DSF_SIGNAL_STRENGTH (RSSI и т.д.) */
    char     firmwareVersion[RC_FW_LEN]; /* RC_DSF_FIRMWARE_VERSION */

    /* Доп. сенсорика */
    float    pm2_5;                      /* RC_DSF_PM2_5 */
    float    pm10;                       /* RC_DSF_PM10 */
    float    tvoc;                       /* RC_DSF_TVOC */
    float    pressure;                   /* RC_DSF_PRESSURE */
    float    ambientLight;               /* RC_DSF_AMBIENT_LIGHT */
    float    noiseLevel;                 /* RC_DSF_NOISE_LEVEL */

    /* Событийные признаки */
    bool     motionDetected;             /* RC_DSF_MOTION_DETECTED */
    bool     smokeDetected;              /* RC_DSF_SMOKE_DETECTED */
    bool     leakDetected;               /* RC_DSF_LEAK_DETECTED */

    char     mode[RC_MODE_LEN];          /* RC_DSF_MODE */
    int32_t  errorCode;                  /* RC_DSF_ERROR_CODE */
    char     statusMessage[RC_STATUS_LEN]; /* RC_DSF_STATUS_MESSAGE */

    /* Маска присутствия опциональных полей */
    uint32_t presenceFlags;
} rc_device_state_t;

//
// ──────────────────────────────────────────────────────────────────────────────
//   ТАРГЕТЫ И ЛИМИТЫ (TARGET)
// ──────────────────────────────────────────────────────────────────────────────
//

/* Флаги присутствия для rc_device_target_t (64 бита, т.к. полей много) */
typedef enum {
    /* Температура */
    RC_TGF_TARGET_TEMP        = (1ull << 0),
    RC_TGF_TARGET_TEMP_MIN    = (1ull << 1),
    RC_TGF_TARGET_TEMP_MAX    = (1ull << 2),
    RC_TGF_TARGET_TEMP_STEP   = (1ull << 3),

    /* Влажность */
    RC_TGF_TARGET_HUM         = (1ull << 4),
    RC_TGF_TARGET_HUM_MIN     = (1ull << 5),
    RC_TGF_TARGET_HUM_MAX     = (1ull << 6),
    RC_TGF_TARGET_HUM_STEP    = (1ull << 7),

    /* CO2 */
    RC_TGF_TARGET_CO2_PPM        = (1ull << 8),
    RC_TGF_TARGET_CO2_PPM_MIN    = (1ull << 9),
    RC_TGF_TARGET_CO2_PPM_MAX    = (1ull <<10),
    RC_TGF_TARGET_CO2_PPM_STEP   = (1ull <<11),

    /* Давление */
    RC_TGF_TARGET_PRESSURE       = (1ull <<12),
    RC_TGF_TARGET_PRESSURE_MIN   = (1ull <<13),
    RC_TGF_TARGET_PRESSURE_MAX   = (1ull <<14),
    RC_TGF_TARGET_PRESSURE_STEP  = (1ull <<15),

    /* Освещённость */
    RC_TGF_TARGET_AMBIENT_LIGHT      = (1ull <<16),
    RC_TGF_TARGET_AMBIENT_LIGHT_MIN  = (1ull <<17),
    RC_TGF_TARGET_AMBIENT_LIGHT_MAX  = (1ull <<18),
    RC_TGF_TARGET_AMBIENT_LIGHT_STEP = (1ull <<19),

    /* Напряжение */
    RC_TGF_TARGET_VOLTAGE        = (1ull <<20),
    RC_TGF_TARGET_VOLTAGE_MIN    = (1ull <<21),
    RC_TGF_TARGET_VOLTAGE_MAX    = (1ull <<22),
    RC_TGF_TARGET_VOLTAGE_STEP   = (1ull <<23),

    /* Вентиляция */
    RC_TGF_TARGET_VENTILATION       = (1ull <<24),
    RC_TGF_TARGET_VENTILATION_MIN   = (1ull <<25),
    RC_TGF_TARGET_VENTILATION_MAX   = (1ull <<26),
    RC_TGF_TARGET_VENTILATION_STEP  = (1ull <<27),

    /* Дистанция */
    RC_TGF_TARGET_DISTANCE       = (1ull <<28),
    RC_TGF_TARGET_DISTANCE_MIN   = (1ull <<29),
    RC_TGF_TARGET_DISTANCE_MAX   = (1ull <<30),
    RC_TGF_TARGET_DISTANCE_STEP  = (1ull <<31),

    /* Скорость км/ч */
    RC_TGF_TARGET_SPEED_KM_H       = (1ull <<32),
    RC_TGF_TARGET_SPEED_KM_H_MIN   = (1ull <<33),
    RC_TGF_TARGET_SPEED_KM_H_MAX   = (1ull <<34),
    RC_TGF_TARGET_SPEED_KM_H_STEP  = (1ull <<35),

    /* Скорость м/с */
    RC_TGF_TARGET_SPEED_MS        = (1ull <<36),
    RC_TGF_TARGET_SPEED_MS_MIN    = (1ull <<37),
    RC_TGF_TARGET_SPEED_MS_MAX    = (1ull <<38),
    RC_TGF_TARGET_SPEED_MS_STEP   = (1ull <<39),

    /* Позиция X/Y/Z */
    RC_TGF_TARGET_POS_X        = (1ull <<40),
    RC_TGF_TARGET_POS_X_MIN    = (1ull <<41),
    RC_TGF_TARGET_POS_X_MAX    = (1ull <<42),
    RC_TGF_TARGET_POS_X_STEP   = (1ull <<43),

    RC_TGF_TARGET_POS_Y        = (1ull <<44),
    RC_TGF_TARGET_POS_Y_MIN    = (1ull <<45),
    RC_TGF_TARGET_POS_Y_MAX    = (1ull <<46),
    RC_TGF_TARGET_POS_Y_STEP   = (1ull <<47),

    RC_TGF_TARGET_POS_Z        = (1ull <<48),
    RC_TGF_TARGET_POS_Z_MIN    = (1ull <<49),
    RC_TGF_TARGET_POS_Z_MAX    = (1ull <<50),
    RC_TGF_TARGET_POS_Z_STEP   = (1ull <<51),

    /* Двоичные таргеты и режим */
    RC_TGF_TARGET_IS_ON        = (1ull <<52),
    RC_TGF_TARGET_IS_OPEN      = (1ull <<53),
    RC_TGF_TARGET_IS_VENT_AUTO = (1ull <<54),
    RC_TGF_TARGET_MODE         = (1ull <<55),
} rc_device_target_flags_t;

/* Таргеты и лимиты устройства (всё, что идёт в /target) */
typedef struct {
    int64_t  id;                         /* опционально — если понадобится */
    char     deviceId[RC_ID_LEN];

    /* ЧИСЛОВЫЕ ТАРГЕТЫ И ЛИМИТЫ */
    float targetTemp,        targetTempMin,        targetTempMax,        targetTempStep;
    float targetHum,         targetHumMin,         targetHumMax,         targetHumStep;
    float targetCo2Ppm,      targetCo2PpmMin,      targetCo2PpmMax,      targetCo2PpmStep;
    float targetPressure,    targetPressureMin,    targetPressureMax,    targetPressureStep;
    float targetAmbientLight,targetAmbientLightMin,targetAmbientLightMax,targetAmbientLightStep;
    float targetVoltage,     targetVoltageMin,     targetVoltageMax,     targetVoltageStep;
    float targetVentilation, targetVentilationMin, targetVentilationMax, targetVentilationStep;
    float targetDistance,    targetDistanceMin,    targetDistanceMax,    targetDistanceStep;
    float targetSpeedKmH,    targetSpeedKmHMin,    targetSpeedKmHMax,    targetSpeedKmHStep;
    float targetSpeedMS,     targetSpeedMSMin,     targetSpeedMSMax,     targetSpeedMSStep;
    float targetPosX,        targetPosXMin,        targetPosXMax,        targetPosXStep;
    float targetPosY,        targetPosYMin,        targetPosYMax,        targetPosYStep;
    float targetPosZ,        targetPosZMin,        targetPosZMax,        targetPosZStep;

    /* ДВОИЧНЫЕ И СТРОКОВЫЕ ТАРГЕТЫ */
    bool  targetIsOn;
    bool  targetIsOpen;
    bool  targetIsVentAuto;
    char  targetMode[RC_MODE_LEN];

    /* Маска присутствия таргет-полей/лимитов */
    uint64_t presenceFlags;
} rc_device_target_t;

//
// ──────────────────────────────────────────────────────────────────────────────
//   УСТРОЙСТВО / СЕРВЕР
// ──────────────────────────────────────────────────────────────────────────────
//

typedef struct {
    int64_t  id;                          /* БД id (если известен) */
    char     deviceId[RC_ID_LEN];
    char     deviceName[RC_NAME_LEN];
    char     type[RC_TYPE_LEN];

    uint32_t lastHeartbeat;               /* Рандомный int от устройства */

    rc_device_state_t  deviceState;       /* текущее состояние (частичное) */
    rc_device_target_t deviceTarget;      /* текущие таргеты/лимиты (частичные) */
} rc_device_t;

typedef struct {
    int64_t  id;
    char     serverId[RC_ID_LEN];
    char     serverName[RC_NAME_LEN];
    uint32_t lastHeartbeat;

    rc_device_t *devices;
    size_t       devicesCount;
} rc_server_t;

//
// ──────────────────────────────────────────────────────────────────────────────
//   ИНИЦИАЛИЗАЦИЯ ПО УМОЛЧАНИЮ
// ──────────────────────────────────────────────────────────────────────────────
//

static inline void rc_device_state_clear(rc_device_state_t *s) {
    if (!s) return;
    s->id = RC_ID_NONE;
    s->deviceId[0] = '\0';
    s->isOn = false;

    s->temp = 0.0f; s->hum = 0.0f; s->isVentAuto = false; s->co2Ppm = 0.0f;
    s->ventilation = 0.0f; s->isOpen = false; s->distance = 0.0f;
    s->batteryLevel = 0.0f; s->voltage = 0.0f; s->signalStrength = 0.0f;
    s->firmwareVersion[0] = '\0';

    s->pm2_5 = 0.0f; s->pm10 = 0.0f; s->tvoc = 0.0f; s->pressure = 0.0f;
    s->ambientLight = 0.0f; s->noiseLevel = 0.0f;

    s->motionDetected = false; s->smokeDetected = false; s->leakDetected = false;
    s->mode[0] = '\0'; s->errorCode = 0; s->statusMessage[0] = '\0';

    s->presenceFlags = 0u;
}

static inline void rc_device_target_clear(rc_device_target_t *t) {
    if (!t) return;
    t->id = RC_ID_NONE; 
    t->deviceId[0] = '\0';

    memset(&t->targetTemp, 0,
            offsetof(rc_device_target_t, targetIsOn) - offsetof(rc_device_target_t, targetTemp));

    t->targetIsOn = false;
    t->targetIsOpen = false;
    t->targetIsVentAuto = false;
    t->targetMode[0] = '\0';

    t->presenceFlags = 0ull;
}

static inline void rc_device_clear(rc_device_t *d) {
    if (!d) return;
    d->id = RC_ID_NONE;
    d->deviceId[0] = '\0';
    d->deviceName[0] = '\0';
    d->type[0] = '\0';
    d->lastHeartbeat = 0u;
    rc_device_state_clear(&d->deviceState);
    rc_device_target_clear(&d->deviceTarget);
}

static inline void rc_server_clear(rc_server_t *s) {
    if (!s) return;
    s->id = RC_ID_NONE;
    s->serverId[0] = '\0';
    s->serverName[0] = '\0';
    s->lastHeartbeat = 0u;
    s->devices = NULL;
    s->devicesCount = 0u;
}

/* Макросы для работы с presenceFlags */
#define RC_DSF_SET(flags, bit)     do { (flags) |=  (bit); } while(0)
#define RC_DSF_CLEAR(flags, bit)   do { (flags) &= ~(bit); } while(0)
#define RC_DSF_HAS(flags, bit)     (((flags) & (bit)) != 0u)

#define RC_TGF_SET(flags, bit)     do { (flags) |=  (bit); } while(0)
#define RC_TGF_CLEAR(flags, bit)   do { (flags) &= ~(bit); } while(0)
#define RC_TGF_HAS(flags, bit)     (((flags) & (bit)) != 0ull)

#ifdef __cplusplus
} /* extern "C" */
#endif
