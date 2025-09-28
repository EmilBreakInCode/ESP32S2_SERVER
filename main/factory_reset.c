// factory_reset.c
#include "factory_reset.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"     // <-- нужен для gpio_config_t/gpio_config/gpio_get_level
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include <inttypes.h>        // <-- PRIu32
#include <string.h>

static const char *TAG = "FACTORY";

static factory_reset_cfg_t s_cfg;
static TaskHandle_t s_task = NULL;

static void factory_task(void *arg)
{
    (void)arg;

    // GPIO как вход с подтяжкой вверх (BOOT тянет к GND при нажатии)
    gpio_config_t io = {
        .pin_bit_mask  = (1ULL << s_cfg.button_gpio),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE
    };
    gpio_config(&io);

    uint32_t held_ms = 0;
    uint32_t last_logged_sec = 0;

    ESP_LOGI(TAG, "Ready for factory reset: hold BOOT on GPIO%ld for %" PRIu32 " ms",
             (long)s_cfg.button_gpio, s_cfg.hold_ms);

    for (;;)
    {
        bool pressed = (gpio_get_level(s_cfg.button_gpio) == 0); // active-low
        if (pressed) {
            held_ms += s_cfg.poll_ms;

            if (held_ms == s_cfg.poll_ms) {
                ESP_LOGW(TAG, "BOOT pressed. Hold about %" PRIu32 " s to erase NVS...",
                         (s_cfg.hold_ms / 1000));
            }

            if (s_cfg.log_countdown) {
                uint32_t sec = held_ms / 1000;
                if (sec != last_logged_sec && (held_ms % 1000 == 0)) {
                    last_logged_sec = sec;
                    ESP_LOGW(TAG, "...%" PRIu32 " / %" PRIu32 " s",
                             sec, (s_cfg.hold_ms / 1000));
                }
            }

            if (held_ms >= s_cfg.hold_ms) {
                ESP_LOGE(TAG, "Hold >= %" PRIu32 " ms — erasing NVS and restarting.", held_ms);

                // Деинициализация и стирание NVS
                esp_err_t er = nvs_flash_deinit(); // OK, если ESP_ERR_NVS_NOT_INITIALIZED
                if (er != ESP_OK && er != ESP_ERR_NVS_NOT_INITIALIZED) {
                    ESP_LOGW(TAG, "nvs_flash_deinit(): %s", esp_err_to_name(er));
                }

                er = nvs_flash_erase();
                if (er != ESP_OK) {
                    ESP_LOGE(TAG, "nvs_flash_erase(): %s", esp_err_to_name(er));
                } else {
                    ESP_LOGI(TAG, "NVS erased.");
                }

                vTaskDelay(pdMS_TO_TICKS(200));
                ESP_LOGI(TAG, "Restarting...");
                esp_restart(); // не возвращается
            }
        } else {
            if (held_ms > 0) {
                ESP_LOGI(TAG, "BOOT released (held %" PRIu32 " ms). Reset cancelled.", held_ms);
            }
            held_ms = 0;
            last_logged_sec = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(s_cfg.poll_ms));
    }
}

esp_err_t factory_reset_start(const factory_reset_cfg_t *cfg)
{
    if (s_task) return ESP_OK; // уже запущено

    // Значения по умолчанию
    factory_reset_cfg_t def = {
        .button_gpio   = FACTORY_RESET_DEFAULT_GPIO,
        .hold_ms       = FACTORY_RESET_DEFAULT_HOLD_MS,
        .poll_ms       = FACTORY_RESET_DEFAULT_POLL_MS,
        .log_countdown = true
    };

    s_cfg = cfg ? *cfg : def;

    // Валидация
    if (s_cfg.button_gpio == GPIO_NUM_NC || s_cfg.hold_ms < 1000 || s_cfg.poll_ms < 10) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xTaskCreate(factory_task, "factory_reset", 2048, NULL, 3, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
