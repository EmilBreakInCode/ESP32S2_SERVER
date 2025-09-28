// device_registry.c
#include "device_registry.h"
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"

#ifndef RC_REG_MAX_DEV
#define RC_REG_MAX_DEV  32
#endif

typedef struct {
    bool   used;
    char   deviceId[64];
    char  *last_raw;     // необязательно для работы, только для диагностики (raw JSON как строка)
    size_t last_raw_len; // длина без завершающего '\0'
} slot_t;

static struct {
    char   serverId[64];
    char   userLogin[64];
    slot_t slots[RC_REG_MAX_DEV];
} R;

static int find(const char *id) {
    if (!id || !id[0]) return -1;
    for (int i = 0; i < RC_REG_MAX_DEV; ++i) {
        if (R.slots[i].used && strcmp(R.slots[i].deviceId, id) == 0) return i;
    }
    return -1;
}

static int alloc_slot(void) {
    for (int i = 0; i < RC_REG_MAX_DEV; ++i) if (!R.slots[i].used) return i;
    return -1;
}

static void free_slot(slot_t *s) {
    if (!s) return;
    if (s->last_raw) { free(s->last_raw); s->last_raw = NULL; }
    s->last_raw_len = 0;
    s->used = false;
    s->deviceId[0] = '\0';
}

void registry_init(const char *serverId, const char *userLogin)
{
    // очистим и освободим все слоты
    for (int i = 0; i < RC_REG_MAX_DEV; ++i) {
        if (R.slots[i].used) free_slot(&R.slots[i]);
    }
    memset(&R, 0, sizeof(R));
    if (serverId)  strncpy(R.serverId,  serverId,  sizeof(R.serverId) - 1);
    if (userLogin) strncpy(R.userLogin, userLogin, sizeof(R.userLogin) - 1);
}

void registry_apply_state(const char *deviceId, const char *raw_json, size_t raw_len)
{
    if (!deviceId || !deviceId[0]) return;

    int idx = find(deviceId);
    if (idx < 0) idx = alloc_slot();
    if (idx < 0) return; // таблица полна

    slot_t *s = &R.slots[idx];
    if (!s->used) {
        s->used = true;
        strncpy(s->deviceId, deviceId, sizeof(s->deviceId) - 1);
        s->last_raw = NULL;
        s->last_raw_len = 0;
    }

    // Сохраняем raw JSON (как есть). Добавим завершающий ноль для удобства.
    if (s->last_raw) { free(s->last_raw); s->last_raw = NULL; s->last_raw_len = 0; }
    if (raw_json && raw_len > 0) {
        s->last_raw = (char*)malloc(raw_len + 1);
        if (s->last_raw) {
            memcpy(s->last_raw, raw_json, raw_len);
            s->last_raw[raw_len] = '\0';
            s->last_raw_len = raw_len;
        }
    }
}

size_t registry_build_state_json(char *buf, size_t cap)
{
    if (!buf || cap < 64) return 0;

    cJSON *root = cJSON_CreateObject();
    if (!root) { buf[0] = '\0'; return 0; }

    cJSON_AddStringToObject(root, "serverId", R.serverId);
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "devices", arr);

    for (int i = 0; i < RC_REG_MAX_DEV; ++i) if (R.slots[i].used) {
        cJSON *jd = cJSON_CreateObject();
        if (!jd) continue;
        cJSON_AddItemToArray(arr, jd);
        cJSON_AddStringToObject(jd, "deviceId", R.slots[i].deviceId);

        // Если нужно — можно добавить «сырой» последний state как строку:
        // cJSON_AddStringToObject(jd, "stateRaw", R.slots[i].last_raw ? R.slots[i].last_raw : "");
        // Но по новой концепции избегаем вложенного парсинга/копирования лишних данных.
    }

    char *out = cJSON_PrintUnformatted(root);
    size_t n = 0;
    if (out) {
        // strlcpy не везде доступна, используем безопасный вариант
        size_t need = strlen(out);
        if (need + 1 <= cap) {
            memcpy(buf, out, need + 1);
            n = need;
        } else {
            n = 0;
            buf[0] = '\0';
        }
        cJSON_free(out);
    } else {
        buf[0] = '\0';
    }
    cJSON_Delete(root);
    return n;
}

size_t registry_device_count(void)
{
    size_t n = 0;
    for (int i = 0; i < RC_REG_MAX_DEV; ++i) if (R.slots[i].used) ++n;
    return n;
}
