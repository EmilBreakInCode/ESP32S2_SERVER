//factory_reset.h
#pragma once
#include "hal/gpio_types.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Значения по умолчанию */
#define FACTORY_RESET_DEFAULT_GPIO     GPIO_NUM_0   // BOOT
#define FACTORY_RESET_DEFAULT_HOLD_MS  5000         // 5 c
#define FACTORY_RESET_DEFAULT_POLL_MS  50           // опрос каждые 50мс

typedef struct {
    gpio_num_t button_gpio;   // обычно GPIO0 (BOOT)
    uint32_t   hold_ms;       // сколько удерживать для сброса
    uint32_t   poll_ms;       // период опроса
    bool       log_countdown; // писать отсчёт в лог раз в секунду
} factory_reset_cfg_t;

/**
 * Запускает фоновую задачу, которая отслеживает удержание кнопки BOOT.
 * При удержании >= hold_ms выполняется: nvs_flash_deinit() -> nvs_flash_erase() -> esp_restart().
 *
 * @param cfg   NULL для значений по умолчанию, либо ваша конфигурация.
 * @return ESP_OK при успешном старте (повторный вызов — тоже OK), иначе код ошибки.
 */
esp_err_t factory_reset_start(const factory_reset_cfg_t *cfg);

#ifdef __cplusplus
}
#endif