// wifi_manager.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MGR_OK = 0,
    WIFI_MGR_ERR_NOT_INITIALIZED = 0x100,
    WIFI_MGR_ERR_INVALID_ARG,
    WIFI_MGR_ERR_TIMEOUT,
    WIFI_MGR_ERR_WIFI_API,
} wifi_mgr_err_t;

typedef enum {
    WIFI_MGR_STA_NONE = 0,
    WIFI_MGR_STA_CONNECTED,
    WIFI_MGR_STA_DISCONNECTED,
} wifi_mgr_sta_state_t;

typedef enum {
    WIFI_MGR_CONN_OK = 0,
    WIFI_MGR_CONN_TIMEOUT,
    WIFI_MGR_CONN_WRONG_PASSWORD,
    WIFI_MGR_CONN_NO_AP_FOUND,
    WIFI_MGR_CONN_UNKNOWN_ERROR,
} wifi_mgr_conn_result_t;

/**
 * Инициализация Wi-Fi и перевод в режим AP+STA.
 * AP будет ЗАПУЩЕН и останется включённым на всём жизненном цикле.
 *
 * @param ap_ssid  SSID точки доступа (1..31 байт)
 * @param ap_pass  Пароль WPA2 (>=8 символов) или пустая строка для open AP
 * @param channel  Канал AP (1..13)
 * @return WIFI_MGR_OK или ошибка
 */
wifi_mgr_err_t wifi_mgr_init_apsta(const char *ap_ssid, const char *ap_pass, uint8_t channel);

/**
 * Подключиться к роутеру в STA (AP остаётся активным).
 * Функция НЕ блокирует надолго: ждёт до timeout_ms, затем возвращает статус.
 *
 * @param ssid       SSID точки
 * @param pass       пароль (может быть пустым для открытой сети)
 * @param timeout_ms таймаут ожидания IP (0 — не ждать IP, только запуск процесса)
 * @return WIFI_MGR_OK / WIFI_MGR_ERR_TIMEOUT / WIFI_MGR_ERR_WIFI_API / ...
 */
wifi_mgr_err_t wifi_mgr_connect_sta(const char *ssid, const char *pass, uint32_t timeout_ms);

/** Отключиться от роутера (AP продолжит работать). */
wifi_mgr_err_t wifi_mgr_disconnect_sta(void);

/** STA подключён и есть IP? */
bool wifi_mgr_sta_has_ip(void);

/** Текущее состояние STA (подключён/нет). */
wifi_mgr_sta_state_t wifi_mgr_get_sta_state(void);

/** Последняя попытка подключения: человеко-читаемый результат. */
wifi_mgr_conn_result_t wifi_mgr_get_last_result(void);

/** Количество клиентов, подключенных к нашему AP. */
int wifi_mgr_ap_client_count(void);

/** Получить IPv4 адрес STA (если есть). Возвращает true при наличии IP. */
bool wifi_mgr_get_sta_ipv4(esp_ip4_addr_t *out_ip);

/** Получить IPv4 адрес AP-интерфейса. */
bool wifi_mgr_get_ap_ipv4(esp_ip4_addr_t *out_ip);

// Установить пользовательские MAC (применяются в момент init_apsta)
// Если mac == NULL → сбросить кастом и использовать заводской.
esp_err_t wifi_mgr_set_custom_mac_sta(const uint8_t mac[6]);
esp_err_t wifi_mgr_set_custom_mac_ap (const uint8_t mac[6]);

// Получить текущие (активные) MAC из драйвера
bool wifi_mgr_get_mac_sta(uint8_t mac_out[6]);
bool wifi_mgr_get_mac_ap (uint8_t mac_out[6]);

#ifdef __cplusplus
} // extern "C"
#endif
