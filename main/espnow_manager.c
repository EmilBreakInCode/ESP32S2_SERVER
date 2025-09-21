// espnow_manager.c
#include "espnow_manager.h"

#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <stdlib.h>


/* ---- фрагментация ---- */
#define RC_ESPNOW_MAX_PAYLOAD 250
#define RC_FRAG_MAGIC         0xA7
#define RC_FRAG_VER           1

/* типы наших кадров для заголовка */
enum { RC_FT_STATE = 1, RC_FT_TARGET = 2 };

/* заголовок каждого фрагмента */
typedef struct __attribute__((packed)) {
    uint8_t  magic;     // 0xA7
    uint8_t  ver;       // 1
    uint8_t  type;      // RC_FT_*
    uint8_t  total;     // N фрагментов
    uint8_t  index;     // 0..N-1
    uint16_t msg_id;    // счётчик
    uint16_t full_len;  // длина полного JSON
    uint16_t crc16;     // CRC полной «склейки» (по желанию, но делаем)
} rc_frag_hdr_t;

enum {
    RC_HDR_SIZE  = sizeof(rc_frag_hdr_t),
    RC_MAX_CHUNK = RC_ESPNOW_MAX_PAYLOAD - RC_HDR_SIZE  // полезная часть на фрагмент
};

/* CRC-16/CCITT-FALSE (0x1021, init 0xFFFF) */
static uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

static const char *TAG = "espnow_mgr";
static const uint8_t BROADCAST_MAC[6] = {0xff,0xff,0xff,0xff,0xff,0xff};

typedef struct {
    char     deviceId[64];
    uint8_t  mac[6];
    uint64_t last_us;
    bool     used;
} sat_entry_t;

static sat_entry_t s_table[RC_MAX_SATELLITES];

#ifndef RC_MAX_REASM
#define RC_MAX_REASM 8
#endif


typedef struct {
    bool     used;
    uint8_t  mac[6];
    uint8_t  type;
    uint16_t msg_id;
    uint16_t full_len;
    uint8_t  total;
    uint16_t crc16;
    uint8_t  received;  // сколько фрагментов получили
    uint8_t *buf;       // full_len
    uint8_t *rcvd;      // массив длиной total (0/1)
    uint64_t deadline_us;
} reasm_t;

static reasm_t s_reasm[RC_MAX_REASM];

static void reasm_reset(reasm_t *r) {
    if (!r) return;
    if (r->buf)  free(r->buf);
    if (r->rcvd) free(r->rcvd);
    memset(r, 0, sizeof(*r));
}

static uint64_t now_us(void) { return (uint64_t)esp_timer_get_time(); }

static reasm_t *reasm_alloc(const uint8_t mac[6], uint8_t type,
                            uint16_t msg_id, uint8_t total,
                            uint16_t full_len, uint16_t crc) {
    uint64_t now = now_us();
    reasm_t *slot = NULL;

    /* ищем свободный/протухший */
    for (int i = 0; i < RC_MAX_REASM; ++i) {
        if (!s_reasm[i].used) { slot = &s_reasm[i]; break; }
        if (s_reasm[i].deadline_us < now) { reasm_reset(&s_reasm[i]); slot = &s_reasm[i]; break; }
    }
    if (!slot) return NULL;

    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    memcpy(slot->mac, mac, 6);
    slot->type = type;
    slot->msg_id = msg_id;
    slot->total = total;
    slot->full_len = full_len;
    slot->crc16 = crc;
    slot->buf = (uint8_t*)malloc(full_len);
    slot->rcvd = (uint8_t*)calloc(total, 1);
    if (!slot->buf || !slot->rcvd) { reasm_reset(slot); return NULL; }
    slot->deadline_us = now + 800000; // 800 мс на сборку
    return slot;
}

static reasm_t *reasm_find(const uint8_t mac[6], uint8_t type, uint16_t msg_id) {
    for (int i = 0; i < RC_MAX_REASM; ++i) {
        if (!s_reasm[i].used) continue;
        if (s_reasm[i].type == type && s_reasm[i].msg_id == msg_id &&
            memcmp(s_reasm[i].mac, mac, 6) == 0)
            return &s_reasm[i];
    }
    return NULL;
}

static bool reasm_feed(reasm_t *r, uint8_t index, const uint8_t *chunk, size_t chunk_len) {
    if (!r || !chunk || index >= r->total) return false;
    size_t off = (size_t)index * RC_MAX_CHUNK;
    if (off + chunk_len > r->full_len) return false;
    if (!r->rcvd[index]) {
        memcpy(r->buf + off, chunk, chunk_len);
        r->rcvd[index] = 1;
        r->received++;
    }
    r->deadline_us = now_us() + 800000;
    return (r->received == r->total);
}

static SemaphoreHandle_t s_mux = NULL;
static bool s_started = false;

static espnow_on_hello_cb_t      s_on_hello = NULL;
static espnow_on_state_json_cb_t s_on_state = NULL;
static espnow_on_target_json_cb_t s_on_target = NULL;

#ifndef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#endif

static const char* ft_to_str(uint8_t ft){
    switch (ft){
        case RC_FT_STATE:  return "STATE";
        case RC_FT_TARGET: return "TARGET";
        default:           return "UNKNOWN";
    }
}

/* optional polling (DIAGNOSTIC): disabled by default */
static TaskHandle_t s_poll_task = NULL;
static uint32_t     s_poll_interval_ms = 2000;
static bool         s_poll_enabled = false;

/* ============ helpers ============ */
static void lock(void){ if (s_mux) xSemaphoreTake(s_mux, portMAX_DELAY); }
static void unlock(void){ if (s_mux) xSemaphoreGive(s_mux); }

static int find_by_device(const char *id) {
    if (!id || !id[0]) return -1;
    for (int i = 0; i < RC_MAX_SATELLITES; ++i)
        if (s_table[i].used && strcmp(s_table[i].deviceId, id) == 0) return i;
    return -1;
}
static int find_by_mac(const uint8_t mac[6]) {
    for (int i = 0; i < RC_MAX_SATELLITES; ++i)
        if (s_table[i].used && memcmp(s_table[i].mac, mac, 6) == 0) return i;
    return -1;
}
static int alloc_slot(void) {
    for (int i = 0; i < RC_MAX_SATELLITES; ++i) if (!s_table[i].used) return i;
    return -1;
}

static esp_err_t ensure_peer(const uint8_t mac[6]) {
    if (!mac) return ESP_ERR_INVALID_ARG;
    if (esp_now_is_peer_exist(mac)) return ESP_OK;
    esp_now_peer_info_t p = (esp_now_peer_info_t){0};
    memcpy(p.peer_addr, mac, 6);
    p.ifidx   = WIFI_IF_STA;  // ВАЖНО для IDF 5.x
    p.channel = 0;            // следовать текущему каналу
    p.encrypt = false;
    return esp_now_add_peer(&p);
}

static esp_err_t send_json_unicast(const uint8_t mac[6], const char *json, size_t len) {
    if (!json || len == 0 || len > 240) return ESP_ERR_INVALID_SIZE;
    esp_err_t e = ensure_peer(mac);
    if (e != ESP_OK) return e;
    return esp_now_send(mac, (const uint8_t*)json, (uint8_t)len);
}

static esp_err_t send_any_unicast(const uint8_t mac[6], uint8_t frame_type,
                                  const char *payload, size_t len)
{
    if (!mac || !payload || !len) return ESP_ERR_INVALID_ARG;
    esp_err_t e = ensure_peer(mac);
    if (e != ESP_OK) return e;

    /* если коротко и это «чистый» JSON — оставляем легаси-беззаголовочный кадр */
    if (len <= RC_MAX_CHUNK && payload[0] == '{') {
        return esp_now_send(mac, (const uint8_t*)payload, (uint8_t)len);
    }

    /* многофрагментная отправка с заголовками */
    uint16_t msg_id = (uint16_t)esp_random(); // можно сделать счётчик
    uint16_t crc = crc16_ccitt((const uint8_t*)payload, len);
    uint8_t total = (uint8_t)((len + RC_MAX_CHUNK - 1) / RC_MAX_CHUNK);

    /* на всякий случай защитимся от «слишком много фрагментов» */
    if (total == 0) total = 1;

    uint8_t frame[RC_ESPNOW_MAX_PAYLOAD];

    for (uint8_t idx = 0; idx < total; ++idx) {
        size_t off = (size_t)idx * RC_MAX_CHUNK;
        size_t chunk = (len - off > RC_MAX_CHUNK) ? RC_MAX_CHUNK : (len - off);

        rc_frag_hdr_t hdr = {
            .magic    = RC_FRAG_MAGIC,
            .ver      = RC_FRAG_VER,
            .type     = frame_type,
            .total    = total,
            .index    = idx,
            .msg_id   = msg_id,
            .full_len = (uint16_t)len,
            .crc16    = crc
        };
        memcpy(frame, &hdr, RC_HDR_SIZE);
        memcpy(frame + RC_HDR_SIZE, payload + off, chunk);

        e = esp_now_send(mac, frame, (uint8_t)(RC_HDR_SIZE + chunk));
        if (e != ESP_OK) return e;

        /* чуть разгрузим эфир */
        esp_rom_delay_us(1000); // 1 ms
    }
    return ESP_OK;
}

static esp_err_t send_json_broadcast(const char *json, size_t len) {
    if (!json || len == 0) return ESP_ERR_INVALID_ARG;
    if (len > RC_MAX_CHUNK) return ESP_ERR_INVALID_SIZE;
    if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
        esp_now_peer_info_t p = (esp_now_peer_info_t){0};
        memcpy(p.peer_addr, BROADCAST_MAC, 6);
        p.ifidx   = WIFI_IF_STA; // тоже ВАЖНО
        p.channel = 0;
        p.encrypt = false;
        esp_now_add_peer(&p);
    }
    return esp_now_send(BROADCAST_MAC, (const uint8_t*)json, (uint8_t)len);
}

static void upsert_satellite(const char *deviceId, const uint8_t mac[6]) {
    lock();
    int idx = find_by_device(deviceId);
    if (idx < 0) idx = find_by_mac(mac);
    if (idx < 0) idx = alloc_slot();
    if (idx >= 0) {
        s_table[idx].used = true;
        strncpy(s_table[idx].deviceId, deviceId, sizeof(s_table[idx].deviceId)-1);
        s_table[idx].deviceId[sizeof(s_table[idx].deviceId)-1] = '\0';
        memcpy(s_table[idx].mac, mac, 6);
        s_table[idx].last_us = now_us();
    } else {
        ESP_LOGW(TAG, "Table full, can’t register %s", deviceId);
    }
    unlock();
}

/* ============ ESPNOW callbacks ============ */

static void on_send_cb(const uint8_t *mac, esp_now_send_status_t status) {
    (void)mac;
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "send status=%d", status);
    }
}

// v5 fix: новая сигнатура колбэка приёма
static void on_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    const uint8_t *mac = info ? info->src_addr : NULL;
    uint8_t *dyn_buf = NULL;
    ESP_LOGI(TAG, "RX %dB from " MACSTR, len, MAC2STR(mac));
    if (!mac || !data || len <= 0) return;

    if (len >= RC_HDR_SIZE) {
    const rc_frag_hdr_t *h = (const rc_frag_hdr_t *)data;
        if (h->magic == RC_FRAG_MAGIC && h->ver == RC_FRAG_VER && h->total >= 1) {
            // подробный лог заголовка фрагмента
            ESP_LOGI(TAG, "FRAG hdr: type=%s(%u) msg_id=%u total=%u idx=%u full_len=%u",
                     ft_to_str(h->type), h->type, h->msg_id, h->total, h->index, h->full_len);

            // корректная проверка broadcast: смотрим адрес назначения
            bool is_broadcast = false;
            if (info && info->des_addr) {
                is_broadcast = (memcmp(info->des_addr, BROADCAST_MAC, 6) == 0);
            }
            if (is_broadcast && h->total > 1) {
                ESP_LOGW(TAG, "drop multi-frag broadcast");
                return;
            }

            const uint8_t *chunk = data + RC_HDR_SIZE;
            size_t chunk_len = (size_t)len - RC_HDR_SIZE;
            reasm_t *ctx = reasm_find(mac, h->type, h->msg_id);
            if (!ctx) ctx = reasm_alloc(mac, h->type, h->msg_id, h->total, h->full_len, h->crc16);
            if (!ctx) { ESP_LOGW(TAG, "no reasm slot"); return; }
            bool done = reasm_feed(ctx, h->index, chunk, chunk_len);
            if (!done) return; // ждём остальные
            if (crc16_ccitt(ctx->buf, ctx->full_len) != ctx->crc16) {
                ESP_LOGW(TAG, "CRC mismatch, drop");
                reasm_reset(ctx);
                return;
            }
            data = ctx->buf;
            len  = ctx->full_len;
            dyn_buf = ctx->buf;
            ctx->buf = NULL;
            reasm_reset(ctx);
        }
    }

    cJSON *root = cJSON_ParseWithLength((const char*)data, len);
    if (!root) return;

    const cJSON *t = cJSON_GetObjectItem(root, "t");
    const char *tt = cJSON_IsString(t) ? t->valuestring : NULL;
    ESP_LOGI(TAG, "RX type='%s'", tt ? tt : "(none)");

    if (tt && strcmp(tt, "hello") == 0) {
        // (опционально) фильтр по serverId
        // const cJSON *j_sid = cJSON_GetObjectItem(root, "serverId");
        const cJSON *j_id  = cJSON_GetObjectItem(root, "deviceId");
        const char  *devId = (cJSON_IsString(j_id) && j_id->valuestring) ? j_id->valuestring : NULL;
        ESP_LOGI(TAG, "HELLO from %s len=%d", devId, len);
        if (devId && devId[0]) {
            upsert_satellite(devId, mac);
            ESP_LOGI(TAG, "HELLO from %s (" MACSTR ")", devId, MAC2STR(mac));
            // ответный hello_ack с каналом
            uint8_t ch = 1;
            wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;         // v5 fix
            esp_wifi_get_channel(&ch, &sec);
            char ack[96];
            int n = snprintf(ack, sizeof(ack), "{\"t\":\"hello_ack\",\"ch\":%u}", (unsigned)ch);
            if (n > 0) send_json_unicast(mac, ack, (size_t)n);
            if (s_on_hello) s_on_hello(devId, mac);
        }
    } else if (tt && strcmp(tt, "state") == 0) {
        const cJSON *j_id  = cJSON_GetObjectItem(root, "deviceId");
        const char  *devId = (cJSON_IsString(j_id) && j_id->valuestring) ? j_id->valuestring : NULL;
        if (devId && devId[0]) {
            upsert_satellite(devId, mac);

            uint8_t ch=1; wifi_second_chan_t sec=WIFI_SECOND_CHAN_NONE;
            esp_wifi_get_channel(&ch, &sec);
            ESP_LOGI(TAG, "STATE from %s len=%d ch=%u", devId, len, (unsigned)ch);
            ESP_LOGD(TAG, "STATE JSON: %.*s", len, (const char*)data);

            if (s_on_state) s_on_state(devId, (const char*)data, len);

            /* ACK, если есть seq */
            const cJSON *j_seq = cJSON_GetObjectItem(root, "seq");
            if (cJSON_IsNumber(j_seq)) {
                char ack[128];
                int n = snprintf(ack, sizeof(ack),
                                 "{\"t\":\"ack\",\"deviceId\":\"%s\",\"seq\":%d}",
                                 devId, j_seq->valueint);
                if (n > 0 && n < (int)sizeof(ack)) {
                    send_json_unicast(mac, ack, (size_t)n);
                }
            }
        }
    } else if (tt && strcmp(tt, "target") == 0) {
    const cJSON *j_id  = cJSON_GetObjectItem(root, "deviceId");
    const char  *devId = (cJSON_IsString(j_id) && j_id->valuestring) ? j_id->valuestring : NULL;
    if (devId && devId[0]) {
        upsert_satellite(devId, mac);
        ESP_LOGI(TAG, "TARGET from %s len=%d", devId, len);
        ESP_LOGD(TAG, "TARGET JSON: %.*s", len, (const char*)data);
        if (s_on_target) {
            s_on_target(devId, (const char*)data, (size_t)len);
        } else {
            ESP_LOGW(TAG, "No target callback set — payload ignored");
        }
    }   // ← вот этой скобки не хватало!
    } else {
        ESP_LOGD(TAG, "RX %.*s", len, (const char*)data);
    }

    cJSON_Delete(root);
    if (dyn_buf) free(dyn_buf);
}

/* ============ polling task (DIAG) ============ */

static void poll_task(void *arg) {
    (void)arg;
    while (1) {
        if (s_poll_enabled) {
            const char *msg = "{\"t\":\"get\"}";
            send_json_broadcast(msg, strlen(msg));
            vTaskDelay(pdMS_TO_TICKS(s_poll_interval_ms));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

/* ============ Public API ============ */

esp_err_t espnow_mgr_start(void)
{
    if (s_started) return ESP_OK;

    memset(s_table, 0, sizeof(s_table));
    if (!s_mux) s_mux = xSemaphoreCreateMutex();

    esp_err_t e = esp_now_init();
    if (e != ESP_OK && e != ESP_ERR_ESPNOW_EXIST) return e;

    esp_now_register_send_cb(on_send_cb);
    esp_now_register_recv_cb(on_recv_cb); // v5 fix: новая сигнатура

    /* polling выключен по умолчанию (push-only) */
    s_poll_enabled = false;
    s_poll_interval_ms = 2000;
    s_poll_task = NULL;

    s_started = true;
    uint8_t ch = 1;
    wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;  // v5 fix
    esp_wifi_get_channel(&ch, &sec);
    ESP_LOGI(TAG, "ESPNOW started (Wi-Fi ch=%u, push-only)", (unsigned)ch);
    return ESP_OK;
}

void espnow_mgr_stop(void)
{
    if (!s_started) return;
    if (s_poll_task) { vTaskDelete(s_poll_task); s_poll_task = NULL; }
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    s_started = false;
}

void espnow_mgr_set_callbacks(espnow_on_hello_cb_t on_hello,
                              espnow_on_state_json_cb_t on_state,
                              espnow_on_target_json_cb_t on_target)
{
    s_on_hello  = on_hello;
    s_on_state  = on_state;
    s_on_target = on_target;
}

esp_err_t espnow_mgr_set_polling(bool enable, uint32_t interval_ms)
{
    s_poll_enabled = enable;
    if (enable && interval_ms >= 100) s_poll_interval_ms = interval_ms;

    if (enable && !s_poll_task) {
        if (xTaskCreate(poll_task, "espnow_poll", 3072, NULL, 4, &s_poll_task) != pdPASS)
            return ESP_ERR_NO_MEM;
    } else if (!enable && s_poll_task) {
        vTaskDelete(s_poll_task);
        s_poll_task = NULL;
    }
    return ESP_OK;
}

esp_err_t espnow_mgr_broadcast_get(void)
{
    const char *msg = "{\"t\":\"get\"}";
    return send_json_broadcast(msg, strlen(msg));
}

esp_err_t espnow_mgr_request_state(const char *deviceId)
{
    if (!deviceId || !deviceId[0]) return ESP_ERR_INVALID_ARG;
    uint8_t mac[6];
    if (espnow_mgr_lookup_mac(deviceId, mac)) {
        const char *msg = "{\"t\":\"get\"}";
        return send_json_unicast(mac, msg, strlen(msg));
    } else {
        char buf[96];
        int n = snprintf(buf, sizeof(buf), "{\"t\":\"get\",\"deviceId\":\"%s\"}", deviceId);
        return send_json_broadcast(buf, (size_t)n);
    }
}

esp_err_t espnow_mgr_send_set_json(const char *deviceId, const char *json, size_t len)
{
    if (!json || !len) return ESP_ERR_INVALID_ARG;

    uint8_t mac[6];
    if (deviceId && deviceId[0] && espnow_mgr_lookup_mac(deviceId, mac)) {
        return send_any_unicast(mac, RC_FT_TARGET, json, len);
    }
    return send_json_broadcast(json, len);
}

bool espnow_mgr_lookup_mac(const char *deviceId, uint8_t mac_out[6])
{
    if (!deviceId || !deviceId[0]) return false;
    bool ok = false;
    lock();
    int idx = find_by_device(deviceId);
    if (idx >= 0) {
        if (mac_out) memcpy(mac_out, s_table[idx].mac, 6);
        ok = true;
    }
    unlock();
    return ok;
}

uint8_t espnow_mgr_current_channel(void)
{
    uint8_t ch = 1;
    wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;   // v5 fix
    esp_wifi_get_channel(&ch, &sec);
    return ch;
}
