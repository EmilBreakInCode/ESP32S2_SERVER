// main/app_nvs.c
#include "app_nvs.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"

#define NS_WIFI  "wifi"
#define NS_APP   "app"

static const char *KEY_MAC_STA = "mac_sta";
static const char *KEY_MAC_AP  = "mac_ap";

esp_err_t app_nvs_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t app_nvs_save_wifi(const char *ssid, const char *pass) {
    if (!ssid || ssid[0] == '\0') return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_WIFI, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    // SSID обязателен
    err = nvs_set_str(h, "ssid", ssid);
    if (err != ESP_OK) { nvs_close(h); return err; }

    // Пароль — опционален: пустая строка => open-сеть, ключ "pass" удаляем
    if (pass && pass[0] != '\0') {
        err = nvs_set_str(h, "pass", pass);
        if (err != ESP_OK) { nvs_close(h); return err; }
    } else {
        esp_err_t e2 = nvs_erase_key(h, "pass");
        if (e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(h);
            return e2;
        }
    }

    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t app_nvs_load_wifi(char *ssid, size_t ssid_len,
                            char *pass, size_t pass_len,
                            bool *has_password) {
    if (!ssid || ssid_len == 0) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_WIFI, NVS_READONLY, &h);
    if (err != ESP_OK) { if (ssid) ssid[0] = '\0'; if (pass) pass[0] = '\0'; if (has_password) *has_password = false; return err; }

    size_t a = ssid_len;
    err = nvs_get_str(h, "ssid", ssid, &a);
    if (err != ESP_OK) { nvs_close(h); if (ssid) ssid[0] = '\0'; if (pass) pass[0] = '\0'; if (has_password) *has_password = false; return err; }

    if (pass && pass_len) {
        size_t b = pass_len;
        esp_err_t ep = nvs_get_str(h, "pass", pass, &b);
        if (ep == ESP_OK) {
            if (has_password) *has_password = true;
        } else {
            pass[0] = '\0';
            if (has_password) *has_password = false;
        }
    } else if (has_password) {
        *has_password = false;
    }

    nvs_close(h);
    return ESP_OK;
}

esp_err_t app_nvs_save_user_login(const char *user_login) {
    if (!user_login) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_APP, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    if (user_login[0] == '\0') {
        // Пустое значение — удаляем ключ
        esp_err_t e2 = nvs_erase_key(h, "userLogin");
        if (e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(h);
            return e2;
        }
    } else {
        err = nvs_set_str(h, "userLogin", user_login);
        if (err != ESP_OK) { nvs_close(h); return err; }
    }

    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t app_nvs_load_user_login(char *user_login, size_t len) {
    if (!user_login || len == 0) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_APP, NVS_READONLY, &h);
    if (err != ESP_OK) { user_login[0] = '\0'; return err; }

    size_t a = len;
    err = nvs_get_str(h, "userLogin", user_login, &a);
    if (err != ESP_OK) user_login[0] = '\0';

    nvs_close(h);
    return err == ESP_OK ? ESP_OK : err;
}

esp_err_t app_nvs_save_server_id(const char *server_id) {
    if (!server_id) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS_APP, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    if (server_id[0] == '\0') {
        esp_err_t e2 = nvs_erase_key(h, "serverId");
        if (e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return e2; }
    } else {
        e = nvs_set_str(h, "serverId", server_id);
        if (e != ESP_OK) { nvs_close(h); return e; }
    }
    e = nvs_commit(h);
    nvs_close(h);
    return e;
}

esp_err_t app_nvs_load_server_id(char *server_id, size_t len) {
    if (!server_id || len == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS_APP, NVS_READONLY, &h);
    if (e != ESP_OK) { server_id[0] = '\0'; return e; }
    size_t a = len;
    e = nvs_get_str(h, "serverId", server_id, &a);
    if (e != ESP_OK) server_id[0] = '\0';
    nvs_close(h);
    return e;
}

static esp_err_t save_mac_key(const char *ns, const char *key, const uint8_t mac[6]) {
    nvs_handle_t h;
    esp_err_t e = nvs_open(ns, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    if (mac) {
        e = nvs_set_blob(h, key, mac, 6);
    } else {
        e = nvs_erase_key(h, key);
        if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    }
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

static esp_err_t load_mac_key(const char *ns, const char *key, uint8_t mac_out[6], bool *present) {
    if (mac_out) memset(mac_out, 0, 6);
    if (present) *present = false;

    nvs_handle_t h;
    esp_err_t e = nvs_open(ns, NVS_READONLY, &h);
    if (e != ESP_OK) return e;

    size_t sz = 6;
    e = nvs_get_blob(h, key, mac_out, &sz);
    nvs_close(h);
    if (e == ESP_OK && sz == 6) {
        if (present) *present = true;
        return ESP_OK;
    }
    return e;
}

esp_err_t app_nvs_save_mac_sta(const uint8_t mac[6]) { return save_mac_key(NS_WIFI, KEY_MAC_STA, mac); }
esp_err_t app_nvs_save_mac_ap (const uint8_t mac[6]) { return save_mac_key(NS_WIFI, KEY_MAC_AP,  mac); }

esp_err_t app_nvs_load_mac_sta(uint8_t mac_out[6], bool *present) { return load_mac_key(NS_WIFI, KEY_MAC_STA, mac_out, present); }
esp_err_t app_nvs_load_mac_ap (uint8_t mac_out[6], bool *present) { return load_mac_key(NS_WIFI, KEY_MAC_AP,  mac_out, present); }