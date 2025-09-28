// device_registry.c
#include "device_registry.h"
#include "rc_model.h"
#include "rc_model_json.h"
#include "cJSON.h"
#include <string.h>

#ifndef RC_REG_MAX_DEV
#define RC_REG_MAX_DEV  32
#endif

typedef struct {
    bool used;
    rc_device_t dev;  // содержит rc_device_state_t внутри
} slot_t;

static struct {
    char serverId[64];
    char userLogin[64];
    slot_t slots[RC_REG_MAX_DEV];
} R;

static int find(const char *id) {
    for (int i=0;i<RC_REG_MAX_DEV;++i)
        if (R.slots[i].used && strcmp(R.slots[i].dev.deviceId,id)==0) return i;
    return -1;
}
static int alloc(void){
    for (int i=0;i<RC_REG_MAX_DEV;++i) if (!R.slots[i].used) return i;
    return -1;
}

void registry_init(const char *serverId, const char *userLogin)
{
    memset(&R, 0, sizeof(R));
    if (serverId)  strncpy(R.serverId,  serverId,  sizeof(R.serverId)-1);
    if (userLogin) strncpy(R.userLogin, userLogin, sizeof(R.userLogin)-1);
}

void registry_apply_state(const rc_device_state_t *st)
{
    if (!st || !st->deviceId[0]) return;
    int idx = find(st->deviceId);
    if (idx < 0) idx = alloc();
    if (idx < 0) return; // таблица полна

    slot_t *s = &R.slots[idx];
    if (!s->used) {
        s->used = true;
        rc_device_clear(&s->dev);
        strncpy(s->dev.deviceId, st->deviceId, sizeof(s->dev.deviceId)-1);
        s->dev.deviceName[0] = '\0';
        s->dev.type[0] = '\0';
    }
    // Копируем состояние
    s->dev.deviceState = *st;
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
        rc_device_t *d = &R.slots[i].dev;

        cJSON *jd = cJSON_CreateObject();
        if (!jd) continue;
        cJSON_AddItemToArray(arr, jd);

        cJSON_AddStringToObject(jd, "deviceId", d->deviceId);
        if (d->deviceName[0]) cJSON_AddStringToObject(jd, "deviceName", d->deviceName);
        if (d->type[0])       cJSON_AddStringToObject(jd, "type", d->type);

        /* Кодируем состояние в временный буфер и парсим один раз */
        char   tmp[768];
        size_t written = 0;

        rc_json_err_t err = rc_json_encode_state(&d->deviceState, tmp, sizeof(tmp), &written, NULL);
        cJSON *js = NULL;
        if (err == RC_JSON_OK && written > 0 && written < sizeof(tmp)) {
            // tmp уже null-terminated по контракту; но на всякий случай:
            tmp[written] = '\0';
            js = cJSON_ParseWithLength(tmp, (int)written);
        }
        if (!js) js = cJSON_CreateObject();  // безопасная заглушка

        /* ВАЖНО: добавляем РОВНО ОДИН РАЗ */
        cJSON_AddItemToObject(jd, "deviceState", js);
    }

    char *out = cJSON_PrintUnformatted(root);
    size_t n = 0;
    if (out) {
        n = strlcpy(buf, out, cap);
        cJSON_free(out);
    } else {
        buf[0] = '\0';
    }
    cJSON_Delete(root);
    return n;
}

size_t registry_device_count(void)
{
    size_t n=0; for (int i=0;i<RC_REG_MAX_DEV;++i) if (R.slots[i].used) ++n; return n;
}
