// rc_model_json.c
#include "rc_model_json.h"
#include <string.h>
#include <math.h>

const rc_json_policy_t RC_JSON_POLICY_DEFAULT = {
    .strictKeys    = false,
    .stableKeyOrder= true,
    .acceptNulls   = false,
    ._reserved     = 0
};

/* ---- helpers ---- */
static inline bool has_flag(const rc_device_state_t *st, uint32_t bit) {
    return (st->presenceFlags & bit) != 0u;
}

/* округление до 2 знаков (максимум) на выходе */
static inline double num2dec(float v) {
    return (double) (roundf(v * 100.0f) / 100.0f);
}
static inline void add_num_if(cJSON *o, const char *k, float v, bool cond){
    if (cond) cJSON_AddNumberToObject(o, k, num2dec(v));
}
static inline void add_bool_if(cJSON *o, const char *k, bool v, bool cond){
    if (cond) cJSON_AddBoolToObject(o, k, v);
}
static inline void add_str_if(cJSON *o, const char *k, const char *v, bool cond){
    if (cond && v && v[0]) cJSON_AddStringToObject(o, k, v);
}

rc_json_err_t rc_json_encode_state(
    const rc_device_state_t *st,
    char *outBuf, size_t outLen, size_t *written,
    const rc_json_policy_t *policy)
{
    (void)policy;
    if (!st || !outBuf || outLen == 0) return RC_JSON_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return RC_JSON_ERR_NO_MEMORY;

    /* Всегда кладём isOn, остальное — по presenceFlags */
    cJSON_AddBoolToObject(root, RCJ_KEY_isOn, st->isOn);

    add_num_if(root, RCJ_KEY_temp,           st->temp,           has_flag(st, RC_DSF_TEMP));
    add_num_if(root, RCJ_KEY_hum,            st->hum,            has_flag(st, RC_DSF_HUM));
    add_bool_if(root,RCJ_KEY_isVentAuto,     st->isVentAuto,     has_flag(st, RC_DSF_IS_VENT_AUTO));
    add_num_if(root, RCJ_KEY_co2Ppm,         st->co2Ppm,         has_flag(st, RC_DSF_CO2_PPM));
    add_num_if(root, RCJ_KEY_ventilation,    st->ventilation,    has_flag(st, RC_DSF_VENTILATION));
    add_bool_if(root,RCJ_KEY_isOpen,         st->isOpen,         has_flag(st, RC_DSF_IS_OPEN));
    add_num_if(root, RCJ_KEY_distance,       st->distance,       has_flag(st, RC_DSF_DISTANCE));
    add_num_if(root, RCJ_KEY_batteryLevel,   st->batteryLevel,   has_flag(st, RC_DSF_BATTERY_LEVEL));
    add_num_if(root, RCJ_KEY_voltage,        st->voltage,        has_flag(st, RC_DSF_VOLTAGE));
    add_num_if(root, RCJ_KEY_signalStrength, st->signalStrength, has_flag(st, RC_DSF_SIGNAL_STRENGTH));
    add_str_if(root, RCJ_KEY_firmwareVersion,st->firmwareVersion,has_flag(st, RC_DSF_FIRMWARE_VERSION));

    add_num_if(root, RCJ_KEY_pm2_5,          st->pm2_5,          has_flag(st, RC_DSF_PM2_5));
    add_num_if(root, RCJ_KEY_pm10,           st->pm10,           has_flag(st, RC_DSF_PM10));
    add_num_if(root, RCJ_KEY_tvoc,           st->tvoc,           has_flag(st, RC_DSF_TVOC));
    add_num_if(root, RCJ_KEY_pressure,       st->pressure,       has_flag(st, RC_DSF_PRESSURE));
    add_num_if(root, RCJ_KEY_ambientLight,   st->ambientLight,   has_flag(st, RC_DSF_AMBIENT_LIGHT));
    add_num_if(root, RCJ_KEY_noiseLevel,     st->noiseLevel,     has_flag(st, RC_DSF_NOISE_LEVEL));

    add_bool_if(root,RCJ_KEY_motionDetected, st->motionDetected, has_flag(st, RC_DSF_MOTION_DETECTED));
    add_bool_if(root,RCJ_KEY_smokeDetected,  st->smokeDetected,  has_flag(st, RC_DSF_SMOKE_DETECTED));
    add_bool_if(root,RCJ_KEY_leakDetected,   st->leakDetected,   has_flag(st, RC_DSF_LEAK_DETECTED));

    add_str_if(root, RCJ_KEY_mode,           st->mode,           has_flag(st, RC_DSF_MODE));
    if (has_flag(st, RC_DSF_ERROR_CODE)) cJSON_AddNumberToObject(root, RCJ_KEY_errorCode, (double)st->errorCode);
    add_str_if(root, RCJ_KEY_statusMessage,  st->statusMessage,  has_flag(st, RC_DSF_STATUS_MESSAGE));

    bool ok = cJSON_PrintPreallocated(root, outBuf, (int)outLen, /*format=*/0);
    if (!ok) {
        char *tmp = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!tmp) return RC_JSON_ERR_NO_MEMORY;
        size_t need = strlen(tmp) + 1;
        if (written) *written = need;
        cJSON_free(tmp);
        return RC_JSON_ERR_BUFFER_TOO_SMALL;
    }
    if (written) *written = strlen(outBuf);
    cJSON_Delete(root);
    return RC_JSON_OK;
}

static bool read_bool(const cJSON *obj, const char *key, bool *out){
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!v) return false;
    if (cJSON_IsBool(v)) { *out = cJSON_IsTrue(v); return true; }
    return false;
}
/* читаем число и сразу ограничиваем до 2 знаков */
static bool read_numf(const cJSON *obj, const char *key, float *out){
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!v) return false;
    if (cJSON_IsNumber(v)) {
        float r = roundf((float)v->valuedouble * 100.0f) / 100.0f;
        *out = r;
        return true;
    }
    return false;
}

static bool read_str_into(const cJSON *obj, const char *key, char *dst, size_t cap){
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!v) return false;
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(dst, v->valuestring, cap-1); dst[cap-1] = '\0';
        return true;
    }
    return false;
}

rc_json_err_t rc_json_decode_state(
    const char *json, size_t len,
    rc_device_state_t *outState,
    const rc_json_policy_t *policy)
{
    if (!json || len == 0 || !outState) return RC_JSON_ERR_INVALID_ARG;
    const rc_json_policy_t *p = policy ? policy : &RC_JSON_POLICY_DEFAULT;
    (void)p;

    cJSON *root = cJSON_ParseWithLength(json, (int)len);
    if (!root) return RC_JSON_ERR_PARSE;

    /* Некоторые отправители могут вкладывать состояние в "deviceState".
       Если такого ключа нет — читаем прямо из корня. */
    const cJSON *obj = cJSON_GetObjectItem(root, "deviceState");
    if (!cJSON_IsObject(obj)) obj = root;

    /* Не трогаем deviceId. Читаем ТОЛЬКО фактические поля состояния. */
    uint32_t flags = outState->presenceFlags;

    bool  btmp;
    float ftmp;

    if (read_bool(obj, RCJ_KEY_isOn, &btmp)) { outState->isOn = btmp; }

    if (read_numf(obj, RCJ_KEY_temp,           &ftmp)) { outState->temp = ftmp; flags |= RC_DSF_TEMP; }
    if (read_numf(obj, RCJ_KEY_hum,            &ftmp)) { outState->hum = ftmp; flags |= RC_DSF_HUM; }
    if (read_bool(obj, RCJ_KEY_isVentAuto,     &btmp)) { outState->isVentAuto = btmp; flags |= RC_DSF_IS_VENT_AUTO; }
    if (read_numf(obj, RCJ_KEY_co2Ppm,         &ftmp)) { outState->co2Ppm = ftmp; flags |= RC_DSF_CO2_PPM; }
    if (read_numf(obj, RCJ_KEY_ventilation,    &ftmp)) { outState->ventilation = ftmp; flags |= RC_DSF_VENTILATION; }
    if (read_bool(obj, RCJ_KEY_isOpen,         &btmp)) { outState->isOpen = btmp; flags |= RC_DSF_IS_OPEN; }
    if (read_numf(obj, RCJ_KEY_distance,       &ftmp)) { outState->distance = ftmp; flags |= RC_DSF_DISTANCE; }
    if (read_numf(obj, RCJ_KEY_batteryLevel,   &ftmp)) { outState->batteryLevel = ftmp; flags |= RC_DSF_BATTERY_LEVEL; }
    if (read_numf(obj, RCJ_KEY_voltage,        &ftmp)) { outState->voltage = ftmp; flags |= RC_DSF_VOLTAGE; }
    if (read_numf(obj, RCJ_KEY_signalStrength, &ftmp)) { outState->signalStrength = ftmp; flags |= RC_DSF_SIGNAL_STRENGTH; }

    char sbuf[RC_STATUS_LEN];
    if (read_str_into(obj, RCJ_KEY_firmwareVersion, sbuf, sizeof(sbuf))) {
        strncpy(outState->firmwareVersion, sbuf, sizeof(outState->firmwareVersion)-1);
        outState->firmwareVersion[sizeof(outState->firmwareVersion)-1] = '\0';
        flags |= RC_DSF_FIRMWARE_VERSION;
    }

    if (read_numf(obj, RCJ_KEY_pm2_5,          &ftmp)) { outState->pm2_5 = ftmp; flags |= RC_DSF_PM2_5; }
    if (read_numf(obj, RCJ_KEY_pm10,           &ftmp)) { outState->pm10 = ftmp; flags |= RC_DSF_PM10; }
    if (read_numf(obj, RCJ_KEY_tvoc,           &ftmp)) { outState->tvoc = ftmp; flags |= RC_DSF_TVOC; }
    if (read_numf(obj, RCJ_KEY_pressure,       &ftmp)) { outState->pressure = ftmp; flags |= RC_DSF_PRESSURE; }
    if (read_numf(obj, RCJ_KEY_ambientLight,   &ftmp)) { outState->ambientLight = ftmp; flags |= RC_DSF_AMBIENT_LIGHT; }
    if (read_numf(obj, RCJ_KEY_noiseLevel,     &ftmp)) { outState->noiseLevel = ftmp; flags |= RC_DSF_NOISE_LEVEL; }

    if (read_bool(obj, RCJ_KEY_motionDetected, &btmp)) { outState->motionDetected = btmp; flags |= RC_DSF_MOTION_DETECTED; }
    if (read_bool(obj, RCJ_KEY_smokeDetected,  &btmp)) { outState->smokeDetected = btmp;  flags |= RC_DSF_SMOKE_DETECTED; }
    if (read_bool(obj, RCJ_KEY_leakDetected,   &btmp)) { outState->leakDetected = btmp;   flags |= RC_DSF_LEAK_DETECTED; }

    if (read_str_into(obj, RCJ_KEY_mode,         sbuf, sizeof(sbuf))) {
        strncpy(outState->mode, sbuf, sizeof(outState->mode)-1);
        outState->mode[sizeof(outState->mode)-1] = '\0';
        flags |= RC_DSF_MODE;
    }

    const cJSON *je = cJSON_GetObjectItem(obj, RCJ_KEY_errorCode);
    if (je && cJSON_IsNumber(je)) { outState->errorCode = (int32_t)je->valuedouble; flags |= RC_DSF_ERROR_CODE; }

    if (read_str_into(obj, RCJ_KEY_statusMessage, sbuf, sizeof(sbuf))) {
        strncpy(outState->statusMessage, sbuf, sizeof(outState->statusMessage)-1);
        outState->statusMessage[sizeof(outState->statusMessage)-1] = '\0';
        flags |= RC_DSF_STATUS_MESSAGE;
    }

    outState->presenceFlags = flags;
    cJSON_Delete(root);
    return RC_JSON_OK;
}

/* ===== TARGET encode/decode ===== */

static inline bool has_tflag(const rc_device_target_t *tg, uint64_t bit) {
    return (tg->presenceFlags & bit) != 0ull;
}

rc_json_err_t rc_json_encode_target(
    const rc_device_target_t *tg,
    char *outBuf, size_t outLen, size_t *written,
    const rc_json_policy_t *policy)
{
    (void)policy;
    if (!tg || !outBuf || outLen == 0) return RC_JSON_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return RC_JSON_ERR_NO_MEMORY;

    /* Значения */
    add_num_if(root, RCJ_KEY_targetTemp,         tg->targetTemp,         has_tflag(tg, RC_TGF_TARGET_TEMP));
    add_num_if(root, RCJ_KEY_targetHum,          tg->targetHum,          has_tflag(tg, RC_TGF_TARGET_HUM));
    add_num_if(root, RCJ_KEY_targetCo2Ppm,       tg->targetCo2Ppm,       has_tflag(tg, RC_TGF_TARGET_CO2_PPM));
    add_num_if(root, RCJ_KEY_targetPressure,     tg->targetPressure,     has_tflag(tg, RC_TGF_TARGET_PRESSURE));
    add_num_if(root, RCJ_KEY_targetAmbientLight, tg->targetAmbientLight, has_tflag(tg, RC_TGF_TARGET_AMBIENT_LIGHT));
    add_num_if(root, RCJ_KEY_targetVoltage,      tg->targetVoltage,      has_tflag(tg, RC_TGF_TARGET_VOLTAGE));
    add_num_if(root, RCJ_KEY_targetVentilation,  tg->targetVentilation,  has_tflag(tg, RC_TGF_TARGET_VENTILATION));
    add_num_if(root, RCJ_KEY_targetDistance,     tg->targetDistance,     has_tflag(tg, RC_TGF_TARGET_DISTANCE));
    add_num_if(root, RCJ_KEY_targetSpeedKmH,     tg->targetSpeedKmH,     has_tflag(tg, RC_TGF_TARGET_SPEED_KM_H));
    add_num_if(root, RCJ_KEY_targetSpeedMS,      tg->targetSpeedMS,      has_tflag(tg, RC_TGF_TARGET_SPEED_MS));
    add_num_if(root, RCJ_KEY_targetPosX,         tg->targetPosX,         has_tflag(tg, RC_TGF_TARGET_POS_X));
    add_num_if(root, RCJ_KEY_targetPosY,         tg->targetPosY,         has_tflag(tg, RC_TGF_TARGET_POS_Y));
    add_num_if(root, RCJ_KEY_targetPosZ,         tg->targetPosZ,         has_tflag(tg, RC_TGF_TARGET_POS_Z));

    add_bool_if(root, RCJ_KEY_targetIsOn,        tg->targetIsOn,         has_tflag(tg, RC_TGF_TARGET_IS_ON));
    add_bool_if(root, RCJ_KEY_targetIsOpen,      tg->targetIsOpen,       has_tflag(tg, RC_TGF_TARGET_IS_OPEN));
    add_bool_if(root, RCJ_KEY_targetIsVentAuto,  tg->targetIsVentAuto,   has_tflag(tg, RC_TGF_TARGET_IS_VENT_AUTO));
    add_str_if (root, RCJ_KEY_targetMode,        tg->targetMode,         has_tflag(tg, RC_TGF_TARGET_MODE));

    /* Лимиты: Min/Max/Step */
    add_num_if(root, RCJ_KEY_targetTempMin,         tg->targetTempMin,         has_tflag(tg, RC_TGF_TARGET_TEMP_MIN));
    add_num_if(root, RCJ_KEY_targetTempMax,         tg->targetTempMax,         has_tflag(tg, RC_TGF_TARGET_TEMP_MAX));
    add_num_if(root, RCJ_KEY_targetTempStep,        tg->targetTempStep,        has_tflag(tg, RC_TGF_TARGET_TEMP_STEP));

    add_num_if(root, RCJ_KEY_targetHumMin,          tg->targetHumMin,          has_tflag(tg, RC_TGF_TARGET_HUM_MIN));
    add_num_if(root, RCJ_KEY_targetHumMax,          tg->targetHumMax,          has_tflag(tg, RC_TGF_TARGET_HUM_MAX));
    add_num_if(root, RCJ_KEY_targetHumStep,         tg->targetHumStep,         has_tflag(tg, RC_TGF_TARGET_HUM_STEP));

    add_num_if(root, RCJ_KEY_targetCo2PpmMin,       tg->targetCo2PpmMin,       has_tflag(tg, RC_TGF_TARGET_CO2_PPM_MIN));
    add_num_if(root, RCJ_KEY_targetCo2PpmMax,       tg->targetCo2PpmMax,       has_tflag(tg, RC_TGF_TARGET_CO2_PPM_MAX));
    add_num_if(root, RCJ_KEY_targetCo2PpmStep,      tg->targetCo2PpmStep,      has_tflag(tg, RC_TGF_TARGET_CO2_PPM_STEP));

    add_num_if(root, RCJ_KEY_targetPressureMin,     tg->targetPressureMin,     has_tflag(tg, RC_TGF_TARGET_PRESSURE_MIN));
    add_num_if(root, RCJ_KEY_targetPressureMax,     tg->targetPressureMax,     has_tflag(tg, RC_TGF_TARGET_PRESSURE_MAX));
    add_num_if(root, RCJ_KEY_targetPressureStep,    tg->targetPressureStep,    has_tflag(tg, RC_TGF_TARGET_PRESSURE_STEP));

    add_num_if(root, RCJ_KEY_targetAmbientLightMin,  tg->targetAmbientLightMin,  has_tflag(tg, RC_TGF_TARGET_AMBIENT_LIGHT_MIN));
    add_num_if(root, RCJ_KEY_targetAmbientLightMax,  tg->targetAmbientLightMax,  has_tflag(tg, RC_TGF_TARGET_AMBIENT_LIGHT_MAX));
    add_num_if(root, RCJ_KEY_targetAmbientLightStep, tg->targetAmbientLightStep, has_tflag(tg, RC_TGF_TARGET_AMBIENT_LIGHT_STEP));

    add_num_if(root, RCJ_KEY_targetVoltageMin,      tg->targetVoltageMin,      has_tflag(tg, RC_TGF_TARGET_VOLTAGE_MIN));
    add_num_if(root, RCJ_KEY_targetVoltageMax,      tg->targetVoltageMax,      has_tflag(tg, RC_TGF_TARGET_VOLTAGE_MAX));
    add_num_if(root, RCJ_KEY_targetVoltageStep,     tg->targetVoltageStep,     has_tflag(tg, RC_TGF_TARGET_VOLTAGE_STEP));

    add_num_if(root, RCJ_KEY_targetVentilationMin,  tg->targetVentilationMin,  has_tflag(tg, RC_TGF_TARGET_VENTILATION_MIN));
    add_num_if(root, RCJ_KEY_targetVentilationMax,  tg->targetVentilationMax,  has_tflag(tg, RC_TGF_TARGET_VENTILATION_MAX));
    add_num_if(root, RCJ_KEY_targetVentilationStep, tg->targetVentilationStep, has_tflag(tg, RC_TGF_TARGET_VENTILATION_STEP));

    add_num_if(root, RCJ_KEY_targetDistanceMin,     tg->targetDistanceMin,     has_tflag(tg, RC_TGF_TARGET_DISTANCE_MIN));
    add_num_if(root, RCJ_KEY_targetDistanceMax,     tg->targetDistanceMax,     has_tflag(tg, RC_TGF_TARGET_DISTANCE_MAX));
    add_num_if(root, RCJ_KEY_targetDistanceStep,    tg->targetDistanceStep,    has_tflag(tg, RC_TGF_TARGET_DISTANCE_STEP));

    add_num_if(root, RCJ_KEY_targetSpeedKmHMin,     tg->targetSpeedKmHMin,     has_tflag(tg, RC_TGF_TARGET_SPEED_KM_H_MIN));
    add_num_if(root, RCJ_KEY_targetSpeedKmHMax,     tg->targetSpeedKmHMax,     has_tflag(tg, RC_TGF_TARGET_SPEED_KM_H_MAX));
    add_num_if(root, RCJ_KEY_targetSpeedKmHStep,    tg->targetSpeedKmHStep,    has_tflag(tg, RC_TGF_TARGET_SPEED_KM_H_STEP));

    add_num_if(root, RCJ_KEY_targetSpeedMSMin,      tg->targetSpeedMSMin,      has_tflag(tg, RC_TGF_TARGET_SPEED_MS_MIN));
    add_num_if(root, RCJ_KEY_targetSpeedMSMax,      tg->targetSpeedMSMax,      has_tflag(tg, RC_TGF_TARGET_SPEED_MS_MAX));
    add_num_if(root, RCJ_KEY_targetSpeedMSStep,     tg->targetSpeedMSStep,     has_tflag(tg, RC_TGF_TARGET_SPEED_MS_STEP));

    add_num_if(root, RCJ_KEY_targetPosXMin,         tg->targetPosXMin,         has_tflag(tg, RC_TGF_TARGET_POS_X_MIN));
    add_num_if(root, RCJ_KEY_targetPosXMax,         tg->targetPosXMax,         has_tflag(tg, RC_TGF_TARGET_POS_X_MAX));
    add_num_if(root, RCJ_KEY_targetPosXStep,        tg->targetPosXStep,        has_tflag(tg, RC_TGF_TARGET_POS_X_STEP));

    add_num_if(root, RCJ_KEY_targetPosYMin,         tg->targetPosYMin,         has_tflag(tg, RC_TGF_TARGET_POS_Y_MIN));
    add_num_if(root, RCJ_KEY_targetPosYMax,         tg->targetPosYMax,         has_tflag(tg, RC_TGF_TARGET_POS_Y_MAX));
    add_num_if(root, RCJ_KEY_targetPosYStep,        tg->targetPosYStep,        has_tflag(tg, RC_TGF_TARGET_POS_Y_STEP));

    add_num_if(root, RCJ_KEY_targetPosZMin,         tg->targetPosZMin,         has_tflag(tg, RC_TGF_TARGET_POS_Z_MIN));
    add_num_if(root, RCJ_KEY_targetPosZMax,         tg->targetPosZMax,         has_tflag(tg, RC_TGF_TARGET_POS_Z_MAX));
    add_num_if(root, RCJ_KEY_targetPosZStep,        tg->targetPosZStep,        has_tflag(tg, RC_TGF_TARGET_POS_Z_STEP));

    /* Печать в предвыделенный буфер */
    bool ok = cJSON_PrintPreallocated(root, outBuf, (int)outLen, 0);
    if (!ok) {
        char *tmp = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!tmp) return RC_JSON_ERR_NO_MEMORY;
        size_t need = strlen(tmp) + 1;
        if (written) *written = need;
        cJSON_free(tmp);
        return RC_JSON_ERR_BUFFER_TOO_SMALL;
    }
    if (written) *written = strlen(outBuf);
    cJSON_Delete(root);
    return RC_JSON_OK;
}

rc_json_err_t rc_json_decode_target(
    const char *json, size_t len,
    rc_device_target_t *outTarget,
    const rc_json_policy_t *policy)
{
    if (!json || len == 0 || !outTarget) return RC_JSON_ERR_INVALID_ARG;
    const rc_json_policy_t *p = policy ? policy : &RC_JSON_POLICY_DEFAULT;
    (void)p;

    cJSON *root = cJSON_ParseWithLength(json, (int)len);
    if (!root) return RC_JSON_ERR_PARSE;

    /* Допускаем вложенность в "deviceTarget", иначе читаем из корня */
    const cJSON *obj = cJSON_GetObjectItem(root, "deviceTarget");
    if (!cJSON_IsObject(obj)) obj = root;

    uint64_t flags = outTarget->presenceFlags;

    bool  btmp;
    float ftmp;
    char  sbuf[RC_MODE_LEN];

    /* Значения */
    if (read_numf(obj, RCJ_KEY_targetTemp,         &ftmp)) { outTarget->targetTemp = ftmp; flags |= RC_TGF_TARGET_TEMP; }
    if (read_numf(obj, RCJ_KEY_targetHum,          &ftmp)) { outTarget->targetHum = ftmp; flags |= RC_TGF_TARGET_HUM; }
    if (read_numf(obj, RCJ_KEY_targetCo2Ppm,       &ftmp)) { outTarget->targetCo2Ppm = ftmp; flags |= RC_TGF_TARGET_CO2_PPM; }
    if (read_numf(obj, RCJ_KEY_targetPressure,     &ftmp)) { outTarget->targetPressure = ftmp; flags |= RC_TGF_TARGET_PRESSURE; }
    if (read_numf(obj, RCJ_KEY_targetAmbientLight, &ftmp)) { outTarget->targetAmbientLight = ftmp; flags |= RC_TGF_TARGET_AMBIENT_LIGHT; }
    if (read_numf(obj, RCJ_KEY_targetVoltage,      &ftmp)) { outTarget->targetVoltage = ftmp; flags |= RC_TGF_TARGET_VOLTAGE; }
    if (read_numf(obj, RCJ_KEY_targetVentilation,  &ftmp)) { outTarget->targetVentilation = ftmp; flags |= RC_TGF_TARGET_VENTILATION; }
    if (read_numf(obj, RCJ_KEY_targetDistance,     &ftmp)) { outTarget->targetDistance = ftmp; flags |= RC_TGF_TARGET_DISTANCE; }
    if (read_numf(obj, RCJ_KEY_targetSpeedKmH,     &ftmp)) { outTarget->targetSpeedKmH = ftmp; flags |= RC_TGF_TARGET_SPEED_KM_H; }
    if (read_numf(obj, RCJ_KEY_targetSpeedMS,      &ftmp)) { outTarget->targetSpeedMS  = ftmp; flags |= RC_TGF_TARGET_SPEED_MS; }
    if (read_numf(obj, RCJ_KEY_targetPosX,         &ftmp)) { outTarget->targetPosX = ftmp; flags |= RC_TGF_TARGET_POS_X; }
    if (read_numf(obj, RCJ_KEY_targetPosY,         &ftmp)) { outTarget->targetPosY = ftmp; flags |= RC_TGF_TARGET_POS_Y; }
    if (read_numf(obj, RCJ_KEY_targetPosZ,         &ftmp)) { outTarget->targetPosZ = ftmp; flags |= RC_TGF_TARGET_POS_Z; }

    if (read_bool(obj, RCJ_KEY_targetIsOn,        &btmp)) { outTarget->targetIsOn = btmp; flags |= RC_TGF_TARGET_IS_ON; }
    if (read_bool(obj, RCJ_KEY_targetIsOpen,      &btmp)) { outTarget->targetIsOpen = btmp; flags |= RC_TGF_TARGET_IS_OPEN; }
    if (read_bool(obj, RCJ_KEY_targetIsVentAuto,  &btmp)) { outTarget->targetIsVentAuto = btmp; flags |= RC_TGF_TARGET_IS_VENT_AUTO; }
    if (read_str_into(obj, RCJ_KEY_targetMode, sbuf, sizeof(sbuf))) {
        strncpy(outTarget->targetMode, sbuf, sizeof(outTarget->targetMode)-1);
        outTarget->targetMode[sizeof(outTarget->targetMode)-1] = '\0';
        flags |= RC_TGF_TARGET_MODE;
    }

    /* Лимиты */
    if (read_numf(obj, RCJ_KEY_targetTempMin,         &ftmp)) { outTarget->targetTempMin = ftmp; flags |= RC_TGF_TARGET_TEMP_MIN; }
    if (read_numf(obj, RCJ_KEY_targetTempMax,         &ftmp)) { outTarget->targetTempMax = ftmp; flags |= RC_TGF_TARGET_TEMP_MAX; }
    if (read_numf(obj, RCJ_KEY_targetTempStep,        &ftmp)) { outTarget->targetTempStep = ftmp; flags |= RC_TGF_TARGET_TEMP_STEP; }

    if (read_numf(obj, RCJ_KEY_targetHumMin,          &ftmp)) { outTarget->targetHumMin = ftmp; flags |= RC_TGF_TARGET_HUM_MIN; }
    if (read_numf(obj, RCJ_KEY_targetHumMax,          &ftmp)) { outTarget->targetHumMax = ftmp; flags |= RC_TGF_TARGET_HUM_MAX; }
    if (read_numf(obj, RCJ_KEY_targetHumStep,         &ftmp)) { outTarget->targetHumStep = ftmp; flags |= RC_TGF_TARGET_HUM_STEP; }

    if (read_numf(obj, RCJ_KEY_targetCo2PpmMin,       &ftmp)) { outTarget->targetCo2PpmMin = ftmp; flags |= RC_TGF_TARGET_CO2_PPM_MIN; }
    if (read_numf(obj, RCJ_KEY_targetCo2PpmMax,       &ftmp)) { outTarget->targetCo2PpmMax = ftmp; flags |= RC_TGF_TARGET_CO2_PPM_MAX; }
    if (read_numf(obj, RCJ_KEY_targetCo2PpmStep,      &ftmp)) { outTarget->targetCo2PpmStep = ftmp; flags |= RC_TGF_TARGET_CO2_PPM_STEP; }

    if (read_numf(obj, RCJ_KEY_targetPressureMin,     &ftmp)) { outTarget->targetPressureMin = ftmp; flags |= RC_TGF_TARGET_PRESSURE_MIN; }
    if (read_numf(obj, RCJ_KEY_targetPressureMax,     &ftmp)) { outTarget->targetPressureMax = ftmp; flags |= RC_TGF_TARGET_PRESSURE_MAX; }
    if (read_numf(obj, RCJ_KEY_targetPressureStep,    &ftmp)) { outTarget->targetPressureStep = ftmp; flags |= RC_TGF_TARGET_PRESSURE_STEP; }

    if (read_numf(obj, RCJ_KEY_targetAmbientLightMin,  &ftmp)) { outTarget->targetAmbientLightMin  = ftmp; flags |= RC_TGF_TARGET_AMBIENT_LIGHT_MIN; }
    if (read_numf(obj, RCJ_KEY_targetAmbientLightMax,  &ftmp)) { outTarget->targetAmbientLightMax  = ftmp; flags |= RC_TGF_TARGET_AMBIENT_LIGHT_MAX; }
    if (read_numf(obj, RCJ_KEY_targetAmbientLightStep, &ftmp)) { outTarget->targetAmbientLightStep = ftmp; flags |= RC_TGF_TARGET_AMBIENT_LIGHT_STEP; }

    if (read_numf(obj, RCJ_KEY_targetVoltageMin,      &ftmp)) { outTarget->targetVoltageMin = ftmp; flags |= RC_TGF_TARGET_VOLTAGE_MIN; }
    if (read_numf(obj, RCJ_KEY_targetVoltageMax,      &ftmp)) { outTarget->targetVoltageMax = ftmp; flags |= RC_TGF_TARGET_VOLTAGE_MAX; }
    if (read_numf(obj, RCJ_KEY_targetVoltageStep,     &ftmp)) { outTarget->targetVoltageStep = ftmp; flags |= RC_TGF_TARGET_VOLTAGE_STEP; }

    if (read_numf(obj, RCJ_KEY_targetVentilationMin,  &ftmp)) { outTarget->targetVentilationMin = ftmp; flags |= RC_TGF_TARGET_VENTILATION_MIN; }
    if (read_numf(obj, RCJ_KEY_targetVentilationMax,  &ftmp)) { outTarget->targetVentilationMax = ftmp; flags |= RC_TGF_TARGET_VENTILATION_MAX; }
    if (read_numf(obj, RCJ_KEY_targetVentilationStep, &ftmp)) { outTarget->targetVentilationStep = ftmp; flags |= RC_TGF_TARGET_VENTILATION_STEP; }

    if (read_numf(obj, RCJ_KEY_targetDistanceMin,     &ftmp)) { outTarget->targetDistanceMin = ftmp; flags |= RC_TGF_TARGET_DISTANCE_MIN; }
    if (read_numf(obj, RCJ_KEY_targetDistanceMax,     &ftmp)) { outTarget->targetDistanceMax = ftmp; flags |= RC_TGF_TARGET_DISTANCE_MAX; }
    if (read_numf(obj, RCJ_KEY_targetDistanceStep,    &ftmp)) { outTarget->targetDistanceStep = ftmp; flags |= RC_TGF_TARGET_DISTANCE_STEP; }

    if (read_numf(obj, RCJ_KEY_targetSpeedKmHMin,     &ftmp)) { outTarget->targetSpeedKmHMin = ftmp; flags |= RC_TGF_TARGET_SPEED_KM_H_MIN; }
    if (read_numf(obj, RCJ_KEY_targetSpeedKmHMax,     &ftmp)) { outTarget->targetSpeedKmHMax = ftmp; flags |= RC_TGF_TARGET_SPEED_KM_H_MAX; }
    if (read_numf(obj, RCJ_KEY_targetSpeedKmHStep,    &ftmp)) { outTarget->targetSpeedKmHStep = ftmp; flags |= RC_TGF_TARGET_SPEED_KM_H_STEP; }

    if (read_numf(obj, RCJ_KEY_targetSpeedMSMin,      &ftmp)) { outTarget->targetSpeedMSMin = ftmp; flags |= RC_TGF_TARGET_SPEED_MS_MIN; }
    if (read_numf(obj, RCJ_KEY_targetSpeedMSMax,      &ftmp)) { outTarget->targetSpeedMSMax = ftmp; flags |= RC_TGF_TARGET_SPEED_MS_MAX; }
    if (read_numf(obj, RCJ_KEY_targetSpeedMSStep,     &ftmp)) { outTarget->targetSpeedMSStep = ftmp; flags |= RC_TGF_TARGET_SPEED_MS_STEP; }

    if (read_numf(obj, RCJ_KEY_targetPosXMin,         &ftmp)) { outTarget->targetPosXMin = ftmp; flags |= RC_TGF_TARGET_POS_X_MIN; }
    if (read_numf(obj, RCJ_KEY_targetPosXMax,         &ftmp)) { outTarget->targetPosXMax = ftmp; flags |= RC_TGF_TARGET_POS_X_MAX; }
    if (read_numf(obj, RCJ_KEY_targetPosXStep,        &ftmp)) { outTarget->targetPosXStep = ftmp; flags |= RC_TGF_TARGET_POS_X_STEP; }

    if (read_numf(obj, RCJ_KEY_targetPosYMin,         &ftmp)) { outTarget->targetPosYMin = ftmp; flags |= RC_TGF_TARGET_POS_Y_MIN; }
    if (read_numf(obj, RCJ_KEY_targetPosYMax,         &ftmp)) { outTarget->targetPosYMax = ftmp; flags |= RC_TGF_TARGET_POS_Y_MAX; }
    if (read_numf(obj, RCJ_KEY_targetPosYStep,        &ftmp)) { outTarget->targetPosYStep = ftmp; flags |= RC_TGF_TARGET_POS_Y_STEP; }

    if (read_numf(obj, RCJ_KEY_targetPosZMin,         &ftmp)) { outTarget->targetPosZMin = ftmp; flags |= RC_TGF_TARGET_POS_Z_MIN; }
    if (read_numf(obj, RCJ_KEY_targetPosZMax,         &ftmp)) { outTarget->targetPosZMax = ftmp; flags |= RC_TGF_TARGET_POS_Z_MAX; }
    if (read_numf(obj, RCJ_KEY_targetPosZStep,        &ftmp)) { outTarget->targetPosZStep = ftmp; flags |= RC_TGF_TARGET_POS_Z_STEP; }

    outTarget->presenceFlags = flags;
    cJSON_Delete(root);
    return RC_JSON_OK;
}