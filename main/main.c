// main.c
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "usb_cdc_init.h"
#include "esp_wifi.h"
#include "wifi_manager.h"
#include "web_portal.h"
#include "mqtt_manager.h"
#include "mqtt_bridge.h"
#include "factory_reset.h"
#include "espnow_manager.h"
#include "device_registry.h"
#include "app_nvs.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "lwip/apps/sntp.h"

// === ВНЕ app_main, ГЛОБАЛЬНО ===
static void start_sntp_once(void) {
    static bool inited = false;
    if (inited) return;

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");  // можно свой NTP
    sntp_init();                             // <-- правильный вызов для lwIP SNTP

    inited = true;
}

void app_main(void)
{
    usb_cdc_init();                      // поднять CDC и привязать консоль к USB
    vTaskDelay(pdMS_TO_TICKS(100));      // маленький запас

    ESP_ERROR_CHECK(app_nvs_init());
    factory_reset_start(NULL);

    wifi_mgr_init_apsta("RostClimat", "2025Rost", 11);
    web_portal_start();

    // === АВТОПОДКЛЮЧЕНИЕ К РОУТЕРУ И ОЖИДАНИЕ IP — ДО MQTT ===
    char ssid[33]={0}, pass[65]={0}; bool has_pass=false;
    app_nvs_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass), &has_pass);

    if (ssid[0]) {
        wifi_mgr_err_t w = wifi_mgr_connect_sta(ssid, has_pass ? pass : "", 10000); // ждём до 10 сек
        if (w != WIFI_MGR_OK) {
            ESP_LOGW("MAIN", "STA connect failed: %d (reason=%d)", w, wifi_mgr_get_last_result());
        }
    }

    if (wifi_mgr_sta_has_ip()) {
        start_sntp_once();                      // <- ВЫЗОВ SNTP ЗДЕСЬ
        vTaskDelay(pdMS_TO_TICKS(500));         // дать времени на синхронизацию
    }
    // ========================================================

    uint8_t mac_hw_sta[6]={0}, mac_sta[6]={0}, mac_ap[6]={0};
    esp_read_mac(mac_hw_sta, ESP_MAC_WIFI_STA);
    wifi_mgr_get_mac_sta(mac_sta);
    wifi_mgr_get_mac_ap(mac_ap);
    ESP_LOGI("MAIN",
             "MACs: factory(STA)=" MACSTR "  active STA=" MACSTR "  active AP=" MACSTR,
             MAC2STR(mac_hw_sta), MAC2STR(mac_sta), MAC2STR(mac_ap));

    uint8_t saved_sta[6]={0}, saved_ap[6]={0};
    bool has_sta=false, has_ap=false;
    app_nvs_load_mac_sta(saved_sta, &has_sta);
    app_nvs_load_mac_ap (saved_ap,  &has_ap);

    char user_login[65]={0}; app_nvs_load_user_login(user_login, sizeof(user_login));
    char server_id[65]={0};  app_nvs_load_server_id(server_id, sizeof(server_id));
    if (server_id[0] == '\0') {
        uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(server_id, sizeof(server_id), "srv-%02X%02X%02X%02X%02X%02X",
                 mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        app_nvs_save_server_id(server_id);
    }

    registry_init(server_id, user_login);
    espnow_mgr_start();
    espnow_mgr_set_polling(true, 3000);

    uint8_t ch = 1;
    wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
    ESP_ERROR_CHECK(esp_wifi_get_channel(&ch, &sec));
    ESP_LOGI("MAIN", "Wi-Fi current channel=%u (sec=%d)", (unsigned)ch, (int)sec);

    const char *BROKER_URI="mqtts://mqtt.rostclimat.ru:8883";
    const char *MQTT_USER ="devices";
    const char *MQTT_PASS ="Dev1ce5213!@GEX";

    // MQTT запускаем только после попытки получить IP и (по возможности) SNTP
    mqtt_mgr_start(BROKER_URI, MQTT_USER, MQTT_PASS, user_login, server_id);
    mqtt_bridge_attach();

    ESP_LOGI("MAIN", "Started. serverId=%s userLogin=%s", server_id, user_login);

    
}
