// app_nvs.h
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_nvs_init(void);

/* Сохранить Wi-Fi креды. pass может быть NULL или "" (open сеть) */
esp_err_t app_nvs_save_wifi(const char *ssid, const char *pass);

/* Загрузить Wi-Fi креды. Если пароль не сохранён — has_password=false */
esp_err_t app_nvs_load_wifi(char *ssid, size_t ssid_len,
                            char *pass, size_t pass_len,
                            bool *has_password);

/* Сохранить/загрузить MQTT userLogin */
esp_err_t app_nvs_save_user_login(const char *user_login);
esp_err_t app_nvs_load_user_login(char *user_login, size_t len);

esp_err_t app_nvs_save_server_id(const char *server_id);
esp_err_t app_nvs_load_server_id(char *server_id, size_t len);

// MAC адреса (NULL → стереть ключ)
esp_err_t app_nvs_save_mac_sta(const uint8_t mac[6]);
esp_err_t app_nvs_save_mac_ap (const uint8_t mac[6]);

// Загрузка (present=true если в NVS есть значение)
esp_err_t app_nvs_load_mac_sta(uint8_t mac_out[6], bool *present);
esp_err_t app_nvs_load_mac_ap (uint8_t mac_out[6], bool *present);

#ifdef __cplusplus
}
#endif

