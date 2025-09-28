// usb_cdc_init.c
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

#include "tinyusb.h"
#include "tusb_cdc_acm.h"

// Хэндл CDC-интерфейса (значение enum, НЕ указатель)
static tinyusb_cdcacm_itf_t s_cdc = TINYUSB_CDC_ACM_0;

// Перенаправление ESP_LOG* в USB CDC
static int cdc_vprintf(const char *fmt, va_list ap)
{
    char buf[256];
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (len <= 0) return len;

    size_t off = 0;
    while (off < (size_t)len) {
        size_t chunk = len - off;
        if (chunk > 64) chunk = 64;
        tinyusb_cdcacm_write_queue(s_cdc, (const uint8_t*)buf + off, chunk);
        tinyusb_cdcacm_write_flush(s_cdc, 0);
        off += chunk;
    }
    return len;
}

void usb_cdc_init(void)
{
    // 1) Самый ранний старт TinyUSB
    const tinyusb_config_t tusb_cfg = { 0 };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    // 2) Поднять CDC-ACM интерфейс #0 (API v1.7.6)
    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,           // в 1.7.6 поле называется cdc_port
        .rx_unread_buf_sz = 256,
        .callback_rx = NULL,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));

    // 3) Время на enumerate
    vTaskDelay(pdMS_TO_TICKS(300));

    // 4) Ведём все ESP_LOG* в CDC
    esp_log_set_vprintf(&cdc_vprintf);

    ESP_LOGI("USB", "USB CDC is up");
}
