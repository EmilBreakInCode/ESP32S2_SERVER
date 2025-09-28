// main/wifi_manager.c
#include "wifi_manager.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"   // для MACSTR/MAC2STR (лог AP клиентов)
#include "app_nvs.h"

static const char *TAG = "wifi_mgr";

/* ---------- Внутреннее состояние ---------- */
static bool s_inited = false;
static esp_netif_t *s_netif_ap  = NULL;
static esp_netif_t *s_netif_sta = NULL;

static EventGroupHandle_t s_evt = NULL;
#define EVT_STA_GOT_IP   BIT0
#define EVT_STA_FAILED   BIT1

static volatile wifi_mgr_sta_state_t   s_sta_state    = WIFI_MGR_STA_NONE;
static volatile wifi_mgr_conn_result_t s_last_result  = WIFI_MGR_CONN_UNKNOWN_ERROR;
static volatile int  s_ap_clients  = 0;
static volatile bool s_sta_has_ip  = false;

static bool     s_has_custom_sta = false;
static bool     s_has_custom_ap  = false;
static uint8_t  s_custom_sta[6]  = {0};
static uint8_t  s_custom_ap[6]   = {0};

static bool mac_is_unicast_local(const uint8_t m[6]) {
    if (!m) return false;
    if (m[0] & 0x01) return false;        // I/G бит: 0 = unicast
    if (!(m[0] & 0x02)) return false;     // U/L бит: 1 = locally administered
    bool all0=true, allF=true;
    for (int i=0;i<6;i++){ if(m[i]!=0x00) all0=false; if(m[i]!=0xFF) allF=false; }
    return !all0 && !allF;
}

/* ---------- Вспомогательные ---------- */
static inline wifi_mgr_err_t _err_wifi(void) { return WIFI_MGR_ERR_WIFI_API; }

static void _events_clear(void) {
    if (s_evt) xEventGroupClearBits(s_evt, EVT_STA_GOT_IP | EVT_STA_FAILED);
    s_sta_has_ip = false;
}

/* ---------- WIFI события ---------- */
static void _on_wifi_event(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    (void)arg; (void)base;

    switch (id) {
    /* --- AP side --- */
    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "AP started");
        break;

    case WIFI_EVENT_AP_STACONNECTED: {
        s_ap_clients++;
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "AP: station " MACSTR " joined, AID=%d, total=%d",
                 MAC2STR(e->mac), e->aid, s_ap_clients);
        break;
    }

    case WIFI_EVENT_AP_STADISCONNECTED: {
        if (s_ap_clients > 0) s_ap_clients--;
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "AP: station " MACSTR " left, AID=%d, total=%d",
                 MAC2STR(e->mac), e->aid, s_ap_clients);
        break;
    }

    /* --- STA side --- */
    case WIFI_EVENT_STA_START:
        s_sta_state   = WIFI_MGR_STA_DISCONNECTED;
        s_last_result = WIFI_MGR_CONN_UNKNOWN_ERROR;
        ESP_LOGI(TAG, "STA started → connect()");
        esp_wifi_connect();  // первая попытка
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        s_sta_state  = WIFI_MGR_STA_DISCONNECTED;
        s_sta_has_ip = false;

        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)data;
        // Базовый маппинг причин (универсально для разных версий IDF)
        switch (disc->reason) {
            case WIFI_REASON_NO_AP_FOUND:
                s_last_result = WIFI_MGR_CONN_NO_AP_FOUND; break;
            case WIFI_REASON_MIC_FAILURE:
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                s_last_result = WIFI_MGR_CONN_WRONG_PASSWORD; break;
            default:
                s_last_result = WIFI_MGR_CONN_TIMEOUT; break;
        }
        ESP_LOGW(TAG, "STA disconnected, reason=%d", disc->reason);
        xEventGroupSetBits(s_evt, EVT_STA_FAILED);
        // Если не «мы сами ушли», сразу просим переподключиться:
        if (disc->reason != WIFI_REASON_ASSOC_LEAVE) {
            esp_wifi_connect();
        }
        break;
    }

    default:
        break;
    }
}

static void _on_ip_event(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    (void)arg; (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_sta_state   = WIFI_MGR_STA_CONNECTED;
        s_last_result = WIFI_MGR_CONN_OK;
        s_sta_has_ip  = true;
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_evt, EVT_STA_GOT_IP);
    } else if (id == IP_EVENT_STA_LOST_IP) {
        s_sta_has_ip  = false;
        s_sta_state   = WIFI_MGR_STA_DISCONNECTED;
        ESP_LOGW(TAG, "STA lost IP → reconnect()");
        esp_wifi_connect();
    }
}

/* ---------- Публичное API ---------- */

wifi_mgr_err_t wifi_mgr_init_apsta(const char *ap_ssid, const char *ap_pass, uint8_t channel)
{
    if (s_inited) {
        ESP_LOGI(TAG, "Already initialized");
        return WIFI_MGR_OK;
    }
    if (!ap_ssid || ap_ssid[0] == '\0' || channel < 1 || channel > 13) {
        return WIFI_MGR_ERR_INVALID_ARG;
    }

    // 1) Базовые подсистемы
    esp_err_t err;
    err = esp_netif_init();                         if (err != ESP_OK) return _err_wifi();
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return _err_wifi();

    // 2) Создаём netif для AP и STA
    s_netif_ap  = esp_netif_create_default_wifi_ap();
    s_netif_sta = esp_netif_create_default_wifi_sta();
    esp_netif_set_default_netif(s_netif_sta);
    if (!s_netif_ap || !s_netif_sta) return _err_wifi();

    // 3) Инициализируем Wi-Fi драйвер
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return _err_wifi();

    // 4) Обработчики событий
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &_on_wifi_event, NULL);
    if (err != ESP_OK) return _err_wifi();
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &_on_ip_event, NULL);
    if (err != ESP_OK) return _err_wifi();
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP,   &_on_ip_event, NULL);
    if (err != ESP_OK) return _err_wifi();

    // 5) EventGroup
    s_evt = xEventGroupCreate();
    if (!s_evt) return _err_wifi();

    // 6) Конфигурируем AP
    wifi_config_t ap_cfg = { 0 };
    strncpy((char*)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.channel        = channel;
    ap_cfg.ap.max_connection = 8;

    size_t pass_len = (ap_pass ? strlen(ap_pass) : 0);
    if (pass_len >= 8) {
        strncpy((char*)ap_cfg.ap.password, ap_pass, sizeof(ap_cfg.ap.password) - 1);
        ap_cfg.ap.authmode        = WIFI_AUTH_WPA2_PSK;
        ap_cfg.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    } else {
        ap_cfg.ap.password[0] = '\0';
        ap_cfg.ap.authmode    = WIFI_AUTH_OPEN;
    }

    // 7) Режим APSTA и старт
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);       if (err != ESP_OK) return _err_wifi();
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg); if (err != ESP_OK) return _err_wifi();
    
    // Подтянем кастомные MAC из NVS, если пользователь сохранял (без зависимости от веб-портала)
    bool have; uint8_t macbuf[6];
    if (app_nvs_load_mac_sta(macbuf, &have) == ESP_OK && have) wifi_mgr_set_custom_mac_sta(macbuf);
    if (app_nvs_load_mac_ap (macbuf, &have) == ESP_OK && have) wifi_mgr_set_custom_mac_ap (macbuf);

    // Установим MAC: обязательно до esp_wifi_start()
    if (s_has_custom_sta) ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, s_custom_sta));
    if (s_has_custom_ap)  {
        // если пользователь случайно указал одинаковые STA/AP — скорректируем AP на +1 в последнем байте
        if (s_has_custom_sta && memcmp(s_custom_ap, s_custom_sta, 6) == 0) {
            uint8_t tmp[6]; memcpy(tmp, s_custom_ap, 6);
            tmp[5] = (uint8_t)(tmp[5] + 1);
            ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_AP, tmp));
        } else {
            ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_AP, s_custom_ap));
        }
    }


    err = esp_wifi_start();                         if (err != ESP_OK) return _err_wifi();

    s_inited = true;

    uint8_t cur_sta[6]={0}, cur_ap[6]={0};
    esp_wifi_get_mac(WIFI_IF_STA, cur_sta);
    esp_wifi_get_mac(WIFI_IF_AP,  cur_ap);
    ESP_LOGI(TAG, "Active MACs after start: STA=" MACSTR " AP=" MACSTR, MAC2STR(cur_sta), MAC2STR(cur_ap));

    ESP_LOGI(TAG, "APSTA ready. AP SSID=\"%s\" ch=%u", ap_ssid, channel);
    return WIFI_MGR_OK;
}

wifi_mgr_err_t wifi_mgr_connect_sta(const char *ssid, const char *pass, uint32_t timeout_ms)
{
    if (!s_inited || !s_netif_sta) return WIFI_MGR_ERR_NOT_INITIALIZED;
    if (!ssid || ssid[0] == '\0')  return WIFI_MGR_ERR_INVALID_ARG;

    // Обеспечим режим APSTA на всякий случай (AP остаётся жив)
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) return _err_wifi();

    wifi_config_t sta_cfg = { 0 };
    strncpy((char*)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    if (pass) strncpy((char*)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password) - 1);

    // WPA2 по умолчанию; для open сети pass="" работает
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;
    sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;   // ищем сеть на всех каналах
    sta_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;       // совместимость с WPA3-Transition
    sta_cfg.sta.bssid_set   = false;                   // не фиксируемся на BSSID
    if (strlen((const char*)sta_cfg.sta.password) == 0) {
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
}

    if (esp_wifi_set_config(WIFI_IF_STA, &sta_cfg) != ESP_OK) return _err_wifi();

    _events_clear();
    s_last_result = WIFI_MGR_CONN_UNKNOWN_ERROR;
    s_sta_state   = WIFI_MGR_STA_DISCONNECTED;

    if (esp_wifi_connect() != ESP_OK) return _err_wifi();

    if (timeout_ms == 0) return WIFI_MGR_OK;

    EventBits_t bits = xEventGroupWaitBits(
        s_evt, EVT_STA_GOT_IP | EVT_STA_FAILED,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );

    if (bits & EVT_STA_GOT_IP)  return WIFI_MGR_OK;
    if (bits & EVT_STA_FAILED)  return WIFI_MGR_ERR_TIMEOUT; // детализация в wifi_mgr_get_last_result()
    return WIFI_MGR_ERR_TIMEOUT;
}

wifi_mgr_err_t wifi_mgr_disconnect_sta(void)
{
    if (!s_inited) return WIFI_MGR_ERR_NOT_INITIALIZED;
    esp_err_t e = esp_wifi_disconnect();
    if (e != ESP_OK && e != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGW(TAG, "esp_wifi_disconnect: %s", esp_err_to_name(e));
        return _err_wifi();
    }
    s_sta_state  = WIFI_MGR_STA_DISCONNECTED;
    s_sta_has_ip = false;
    return WIFI_MGR_OK;
}

bool wifi_mgr_sta_has_ip(void)
{
    return s_sta_has_ip;
}

wifi_mgr_sta_state_t wifi_mgr_get_sta_state(void)
{
    return s_sta_state;
}

wifi_mgr_conn_result_t wifi_mgr_get_last_result(void)
{
    return s_last_result;
}

int wifi_mgr_ap_client_count(void)
{
    return s_ap_clients;
}

bool wifi_mgr_get_sta_ipv4(esp_ip4_addr_t *out_ip)
{
    if (!s_netif_sta || !out_ip) return false;
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_netif_sta, &info) == ESP_OK) {
        *out_ip = info.ip;
        return info.ip.addr != 0;
    }
    return false;
}

bool wifi_mgr_get_ap_ipv4(esp_ip4_addr_t *out_ip)
{
    if (!s_netif_ap || !out_ip) return false;
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_netif_ap, &info) == ESP_OK) {
        *out_ip = info.ip;
        return info.ip.addr != 0;
    }
    return false;
}

esp_err_t wifi_mgr_set_custom_mac_sta(const uint8_t mac[6]) {
    if (mac) {
        if (!mac_is_unicast_local(mac)) return ESP_ERR_INVALID_ARG;
        memcpy(s_custom_sta, mac, 6);
        s_has_custom_sta = true;
    } else {
        memset(s_custom_sta, 0, 6);
        s_has_custom_sta = false;
    }
    return ESP_OK;
}
esp_err_t wifi_mgr_set_custom_mac_ap(const uint8_t mac[6]) {
    if (mac) {
        if (!mac_is_unicast_local(mac)) return ESP_ERR_INVALID_ARG;
        memcpy(s_custom_ap, mac, 6);
        s_has_custom_ap = true;
    } else {
        memset(s_custom_ap, 0, 6);
        s_has_custom_ap = false;
    }
    return ESP_OK;
}

bool wifi_mgr_get_mac_sta(uint8_t mac_out[6]) {
    if (!mac_out) return false;
    return esp_wifi_get_mac(WIFI_IF_STA, mac_out) == ESP_OK;
}
bool wifi_mgr_get_mac_ap(uint8_t mac_out[6]) {
    if (!mac_out) return false;
    return esp_wifi_get_mac(WIFI_IF_AP, mac_out) == ESP_OK;
}
