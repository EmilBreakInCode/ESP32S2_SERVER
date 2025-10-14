// internal_device.c
#include "internal_device.h"
#include "mqtt_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_random.h"

#if __has_include("internal_profile.h")
#  include "internal_profile.h"
#endif

#ifndef INTDEV_ENABLE
#define INTDEV_ENABLE 0
#endif
#ifndef INTDEV_DEVICE_ID
#define INTDEV_DEVICE_ID "int-temp-01"
#endif
#ifndef INTDEV_FW_VERSION
#define INTDEV_FW_VERSION "intdev-1.0.0"
#endif
#ifndef INTDEV_STATE_PERIOD_MS
#define INTDEV_STATE_PERIOD_MS 3000
#endif
#ifndef INTDEV_TARGET_PERIOD_MS
#define INTDEV_TARGET_PERIOD_MS 10000
#endif
#ifndef INTDEV_USE_DS18B20
#define INTDEV_USE_DS18B20 0
#endif
#ifndef INTDEV_ONEWIRE_GPIO
#define INTDEV_ONEWIRE_GPIO 16
#endif
// ВКЛ./ВЫКЛ. печать в монитор порта
#ifndef INTDEV_PRINT_TO_SERIAL
#define INTDEV_PRINT_TO_SERIAL 1
#endif

static const char *TAG = "intdev";

#if INTDEV_ENABLE

/* =========================
 *  Общее состояние/таргеты
 * ========================= */
typedef struct {
    bool  isOn;
    float tempC;
    char  fw[32];
    uint32_t seq;
    uint32_t heartbeatNonce;   // ← числовой heartbeatNonce как в Arduino
} intdev_state_t;

typedef struct {
    bool targetIsOn;
} intdev_targets_t;

static struct {
    intdev_state_t  S;
    intdev_targets_t T;
    TaskHandle_t     task;
} G;

/* округление */
static inline double f2(double v){
    if (!isfinite(v)) return NAN;
    double x = floor(v * 10.0 + 0.5) / 10.0;
    if (x == 0.0) x = 0.0;
    return x;
}

/* =========================
 *    1-Wire DS18B20
 * ========================= */
#if INTDEV_USE_DS18B20
static inline void ow_delay_us(uint32_t us){ esp_rom_delay_us(us); }

static void ow_set_output(void){
    gpio_set_direction(INTDEV_ONEWIRE_GPIO, GPIO_MODE_OUTPUT);
}

static void ow_set_input(void){
    gpio_set_direction(INTDEV_ONEWIRE_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(INTDEV_ONEWIRE_GPIO, GPIO_PULLUP_ONLY);
}

static inline void ow_write_low(void){ gpio_set_level(INTDEV_ONEWIRE_GPIO, 0); }

static bool ow_reset_presence(void){
    ow_set_output();
    ow_write_low();
    ow_delay_us(480);
    ow_set_input();
    ow_delay_us(70);
    int presence = gpio_get_level(INTDEV_ONEWIRE_GPIO) == 0; // 0 — присутствие
    ow_delay_us(410);
    return presence;
}

static void ow_write_bit(int b){
    ow_set_output();
    ow_write_low();
    if (b){
        ow_delay_us(6);
        ow_set_input();
        ow_delay_us(64);
    } else {
        ow_delay_us(60);
        ow_set_input();
        ow_delay_us(10);
    }
}

static int ow_read_bit(void){
    int r;
    ow_set_output();
    ow_write_low();
    ow_delay_us(6);
    ow_set_input();
    ow_delay_us(9);
    r = gpio_get_level(INTDEV_ONEWIRE_GPIO);
    ow_delay_us(55);
    return r & 1;
}

static void ow_write_byte(uint8_t v){
    for (int i=0;i<8;i++){
        ow_write_bit(v & 1);
        v >>= 1;
    }
}

static uint8_t ow_read_byte(void){
    uint8_t v=0;
    for (int i=0;i<8;i++){
        v |= (ow_read_bit() << i);
    }
    return v;
}

static bool ds18b20_convert_t_blocking(void){
    if (!ow_reset_presence()) return false;
    ow_write_byte(0xCC); // SKIP ROM
    ow_write_byte(0x44); // CONVERT T
    vTaskDelay(pdMS_TO_TICKS(760)); // ждём окончание конверсии (12 бит)
    return true;
}

static bool ds18b20_read_temp(float *outC){
    if (!ow_reset_presence()) return false;
    ow_write_byte(0xCC); // SKIP ROM
    ow_write_byte(0xBE); // READ SCRATCHPAD
    uint8_t lo = ow_read_byte();
    uint8_t hi = ow_read_byte();
    // дочитаем оставшиеся 7 байт (в т.ч. CRC), но не используем
    for (int i=0;i<7;i++) (void)ow_read_byte();

    int16_t raw = (int16_t)((hi << 8) | lo);
    float c = (float)raw / 16.0f; // 12-bit
    if (outC) *outC = c;
    return true;
}
#endif // INTDEV_USE_DS18B20

static inline void make_nonce(uint32_t *nonce){
    *nonce = esp_random();  // Генерация случайного числа для nonce
}

/* =========================
 *   Сборка JSON payload’ов
 * ========================= */
static size_t build_state_json(char *buf, size_t cap){
    if (!buf || cap < 128) return 0;

    cJSON *root = cJSON_CreateObject();
    if (!root) return 0;

    cJSON_AddStringToObject(root, "t", "state");
    cJSON_AddStringToObject(root, "deviceId", INTDEV_DEVICE_ID);
    cJSON_AddNumberToObject(root, "seq", (double)G.S.seq++);

    cJSON_AddBoolToObject(root, "isOn", G.S.isOn);
    cJSON_AddBoolToObject(root, "targetIsOn", G.T.targetIsOn);

    // temp: ровно 1 знак после запятой, без двоичного «хвоста»
    if (isnan(G.S.tempC)) {
        cJSON_AddNullToObject(root, "temp");
    } else {
        char tbuf[24];
        snprintf(tbuf, sizeof(tbuf), "%.1f", (double)G.S.tempC);
        cJSON_AddRawToObject(root, "temp", tbuf);  // кладём число как сырой литерал "20.1"
    }

    cJSON_AddStringToObject(root, "firmwareVersion", G.S.fw);

    // ВАЖНО: как в Arduino — heartbeatNonce (число)
    cJSON_AddNumberToObject(root, "heartbeatNonce", (double)G.S.heartbeatNonce);

    char *out = cJSON_PrintUnformatted(root);
    size_t n = 0;
    if (out){
        size_t need = strlen(out);
        if (need + 1 <= cap){
            memcpy(buf, out, need + 1);
            n = need;
        }
        cJSON_free(out);
    }
    cJSON_Delete(root);
    return n;
}


static size_t build_target_snapshot_json(char *buf, size_t cap){
    if (!buf || cap < 64) return 0;
    cJSON *d = cJSON_CreateObject();
    if (!d) return 0;

    cJSON_AddStringToObject(d, "t", "target");
    cJSON_AddStringToObject(d, "deviceId", INTDEV_DEVICE_ID);
    cJSON_AddBoolToObject(d, "targetIsOn", G.T.targetIsOn);

    char *out = cJSON_PrintUnformatted(d);
    size_t n = 0;
    if (out){
        size_t need = strlen(out);
        if (need + 1 <= cap){
            memcpy(buf, out, need + 1);
            n = need;
        }
        cJSON_free(out);
    }
    cJSON_Delete(d);
    return n;
}

/* =========================
 *   Публикации в MQTT
 * ========================= */
static void publish_state(void){
    if (!mqtt_mgr_is_connected()) return;
    static char buf[512];
    size_t n = build_state_json(buf, sizeof(buf));
    if (n) mqtt_mgr_publish_state_payload(buf, n);
}

static void publish_target_snapshot(void){
    if (!mqtt_mgr_is_connected()) return;
    static char buf[256];
    size_t n = build_target_snapshot_json(buf, sizeof(buf));
    if (n) mqtt_mgr_publish_target_payload(buf, n);
}

/* =========================
 *   Основная задача
 * ========================= */
static void intdev_task(void *arg){
    (void)arg;
    ESP_LOGI(TAG, "Internal device started: deviceId=%s", INTDEV_DEVICE_ID);

#if INTDEV_USE_DS18B20
    ESP_LOGI(TAG, "DS18B20 on GPIO%d", INTDEV_ONEWIRE_GPIO);
    gpio_config_t io = {
        .pin_bit_mask  = (1ULL << INTDEV_ONEWIRE_GPIO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
#else
    ESP_LOGI(TAG, "DS18B20 disabled (INTDEV_USE_DS18B20=0)");
#endif

    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t nextMeasure = now;  // первая попытка сразу
    uint32_t nextState   = now;  // и публикация тоже сразу

    for (;;){
        now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        // ----- Измерение -----
        if ((int32_t)(now - nextMeasure) >= 0) {
            if (G.S.isOn){
            #if INTDEV_USE_DS18B20
                if (ds18b20_convert_t_blocking()){
                    float tC = NAN;
                    if (ds18b20_read_temp(&tC)){
                        G.S.tempC = tC;
                    #if INTDEV_PRINT_TO_SERIAL
                        ESP_LOGI(TAG, "[temp] %.2f C", G.S.tempC);
                    #endif
                    } else {
                        G.S.tempC = NAN;
                    #if INTDEV_PRINT_TO_SERIAL
                        ESP_LOGW(TAG, "[temp] read failed");
                    #endif
                    }
                } else {
                    G.S.tempC = NAN;
                #if INTDEV_PRINT_TO_SERIAL
                    ESP_LOGW(TAG, "[temp] sensor not found");
                #endif
                }
            #else
                G.S.tempC = NAN;
            #if INTDEV_PRINT_TO_SERIAL
                ESP_LOGI(TAG, "[temp] N/A (no DS18B20)");
            #endif
            #endif
            } else {
                G.S.tempC = NAN;
            #if INTDEV_PRINT_TO_SERIAL
                ESP_LOGI(TAG, "[temp] OFF (targetIsOn=false)");
            #endif
            }

            nextMeasure += INTDEV_STATE_PERIOD_MS;
        }

        // ----- Публикация state -----
        if ((int32_t)(now - nextState) >= 0){
            // новый heartbeatNonce на каждую публикацию
            G.S.heartbeatNonce = esp_random() & 0x7FFFFFFF;

            publish_state();  // Публикация состояния
            nextState += INTDEV_STATE_PERIOD_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* =========================
 *   Публичные функции
 * ========================= */
void internal_dev_init(void){
    memset(&G, 0, sizeof(G));
    G.S.isOn = true;
    G.T.targetIsOn = true;
    strncpy(G.S.fw, INTDEV_FW_VERSION, sizeof(G.S.fw)-1);
    G.S.seq = 1;

    // стартовый heartbeatNonce
    G.S.heartbeatNonce = esp_random() & 0x7FFFFFFF;
}

void internal_dev_start(void){
    if (G.task) return;
    if (xTaskCreate(intdev_task, "intdev", 4096, NULL, 4, &G.task) != pdPASS){
        ESP_LOGE(TAG, "Failed to start intdev task");
        G.task = NULL;
    }
}

bool internal_dev_is_enabled(void){ return true; }
const char* internal_dev_id(void){ return INTDEV_DEVICE_ID; }

void internal_dev_on_mqtt_target(const char *json, size_t len){
    if (!json || !len) return;

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return;

    const cJSON *jDev = cJSON_GetObjectItem(root, "deviceId");
    if (cJSON_IsString(jDev) && jDev->valuestring){
        if (strcmp(jDev->valuestring, INTDEV_DEVICE_ID) != 0){
            cJSON_Delete(root);
            return;
        }
    }

    const cJSON *jTo = cJSON_GetObjectItem(root, "targetIsOn");
    if (cJSON_IsBool(jTo)){
        G.T.targetIsOn = cJSON_IsTrue(jTo);
        G.S.isOn = G.T.targetIsOn;
#if INTDEV_PRINT_TO_SERIAL
        ESP_LOGI(TAG, "[target] targetIsOn=%s", G.S.isOn ? "true" : "false");
#endif
    }

    publish_state();

    cJSON_Delete(root);
}

void internal_dev_publish_now(void){
    publish_state();
    publish_target_snapshot();
}

#else // INTDEV_ENABLE == 0

void internal_dev_init(void) {}
void internal_dev_start(void) {}
bool internal_dev_is_enabled(void){ return false; }
const char* internal_dev_id(void){ return ""; }
void internal_dev_on_mqtt_target(const char *json, size_t len){ (void)json; (void)len; }
void internal_dev_publish_now(void){}

#endif // INTDEV_ENABLE
