// web_portal.c
#include "web_portal.h"
#include <string.h>
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdio.h>        // snprintf
#include "esp_system.h"   // esp_restart
#include "app_nvs.h"
#include "wifi_manager.h"

static const char *TAG = "web_portal";
static httpd_handle_t s_server = NULL;

/* ===== embedded static files =====
 * Эти символы создаёт IDF при EMBED_FILES "web/index.html" "web/app.js" "web/style.css"
 */
extern const unsigned char _binary_index_html_start[] asm("_binary_index_html_start");
extern const unsigned char _binary_index_html_end[]   asm("_binary_index_html_end");
extern const unsigned char _binary_app_js_start[]     asm("_binary_app_js_start");
extern const unsigned char _binary_app_js_end[]       asm("_binary_app_js_end");
extern const unsigned char _binary_style_css_start[]  asm("_binary_style_css_start");
extern const unsigned char _binary_style_css_end[]    asm("_binary_style_css_end");

typedef struct {
    const char *uri;
    const unsigned char *start;
    const unsigned char *end;
    const char *ctype;
} asset_t;

static const asset_t s_assets[] = {
    {"/",           _binary_index_html_start, _binary_index_html_end, "text/html; charset=utf-8"},
    {"/index.html", _binary_index_html_start, _binary_index_html_end, "text/html; charset=utf-8"},
    {"/app.js",     _binary_app_js_start,     _binary_app_js_end,     "application/javascript"},
    {"/style.css",  _binary_style_css_start,  _binary_style_css_end,  "text/css"},
};

/* =================== Вспомогалки =================== */
static void add_cors_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,PUT,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

/* Форматирует IPv4 в строку (без аллокаций) */
static void ip4_to_str(char *dst, size_t n, esp_ip4_addr_t ip) {
    snprintf(dst, n, "%u.%u.%u.%u",
             (unsigned)((ip.addr) & 0xFF),
             (unsigned)((ip.addr >> 8) & 0xFF),
             (unsigned)((ip.addr >> 16) & 0xFF),
             (unsigned)((ip.addr >> 24) & 0xFF));
}

static void mac_to_str(char *dst, size_t n, const uint8_t m[6]) {
    if (!dst || n == 0) return;
    if (!m || n < 18) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, n, MACSTR, MAC2STR(m));
}

static bool parse_mac_str(const char *s, uint8_t out[6]) {
    if (!s || strlen(s)!=17) return false;
    unsigned v[6];
    if (sscanf(s,"%2x:%2x:%2x:%2x:%2x:%2x",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5])!=6) return false;
    for (int i=0;i<6;i++) out[i]=(uint8_t)v[i];
    if (out[0] & 0x01) return false;      // multicast нельзя
    if (!(out[0] & 0x02)) return false;   // требуем local-admin
    return true;
}

/* =================== JSON helpers =================== */
static cJSON* read_json_body(httpd_req_t *req) {
    int total = req->content_len;
    if (total <= 0 || total > 4096) return NULL;
    char *buf = (char*)malloc(total + 1);
    if (!buf) return NULL;

    int off = 0;
    while (off < total) {
        int r = httpd_req_recv(req, buf + off, total - off);
        if (r <= 0) { free(buf); return NULL; }
        off += r;
    }
    buf[off] = '\0';
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    return json;
}

static const char* conn_result_to_str(wifi_mgr_conn_result_t r) {
    switch (r) {
        case WIFI_MGR_CONN_OK: return "ok";
        case WIFI_MGR_CONN_TIMEOUT: return "timeout";
        case WIFI_MGR_CONN_WRONG_PASSWORD: return "wrong_password";
        case WIFI_MGR_CONN_NO_AP_FOUND: return "no_ap_found";
        default: return "unknown";
    }
}

/* =================== Static files handler =================== */
static esp_err_t handle_static(httpd_req_t *req) {
    const char *u = req->uri;
    if (u[0] == '\0' || strcmp(u, "/") == 0) u = "/index.html";
    for (size_t i=0;i<sizeof(s_assets)/sizeof(s_assets[0]);++i) {
        if (strcmp(u, s_assets[i].uri) == 0) {
            httpd_resp_set_type(req, s_assets[i].ctype);
            httpd_resp_set_hdr(req, "Cache-Control", "no-store");
            size_t len = (size_t)(s_assets[i].end - s_assets[i].start);
            return httpd_resp_send(req, (const char*)s_assets[i].start, len);
        }
    }
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_OK;
}

static esp_err_t handle_options(httpd_req_t *req) {
    add_cors_headers(req);
    return httpd_resp_send(req, NULL, 0);
}

/* =================== API Handlers =================== */

static esp_err_t handle_get_config(httpd_req_t *req) {
    add_cors_headers(req);

    /* Wi-Fi сохранённые */
    char ssid[33] = {0};
    char pass[65] = {0};
    bool has_pass = false;
    app_nvs_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass), &has_pass);

    /* userLogin */
    char userLogin[65] = {0};
    app_nvs_load_user_login(userLogin, sizeof(userLogin));

    /* serverId */
    char server_id[65]={0};
    app_nvs_load_server_id(server_id, sizeof(server_id));

    /* Статусы */
    bool connected = wifi_mgr_sta_has_ip();
    wifi_ap_record_t ap = {0};
    char sta_ssid[33] = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        strncpy(sta_ssid, (const char*)ap.ssid, sizeof(sta_ssid)-1);
    }
    esp_ip4_addr_t ip = { .addr = 0 };
    char ipstr[16] = "0.0.0.0";
    if (wifi_mgr_get_sta_ipv4(&ip)) {
        ip4_to_str(ipstr, sizeof(ipstr), ip);
    }

    /* Активные MAC */
    uint8_t mac_sta[6]={0}, mac_ap[6]={0};
    char mac_sta_act[18]="", mac_ap_act[18]="";
    if (wifi_mgr_get_mac_sta(mac_sta)) mac_to_str(mac_sta_act, sizeof(mac_sta_act), mac_sta);
    if (wifi_mgr_get_mac_ap (mac_ap))  mac_to_str(mac_ap_act,  sizeof(mac_ap_act),  mac_ap);

    /* Сохранённые MAC */
    uint8_t mac_saved_sta[6]={0}, mac_saved_ap[6]={0};
    bool has_sta=false, has_ap=false;
    app_nvs_load_mac_sta(mac_saved_sta, &has_sta);
    app_nvs_load_mac_ap (mac_saved_ap,  &has_ap);
    char mac_sta_saved[18]="", mac_ap_saved[18]="";
    if (has_sta) mac_to_str(mac_sta_saved, sizeof(mac_sta_saved), mac_saved_sta);
    if (has_ap)  mac_to_str(mac_ap_saved,  sizeof(mac_ap_saved),  mac_saved_ap);

    /* Сборка JSON */
    cJSON *root = cJSON_CreateObject();

    cJSON *server = cJSON_CreateObject();
    cJSON_AddStringToObject(server, "serverId", server_id);
    cJSON_AddItemToObject(root, "server", server);

    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "ssid", ssid);
    cJSON_AddBoolToObject  (wifi, "passwordSaved", has_pass);
    cJSON_AddBoolToObject  (wifi, "connected", connected);
    cJSON_AddStringToObject(wifi, "staSsid", sta_ssid[0]?sta_ssid:"");
    cJSON_AddStringToObject(wifi, "ip", ipstr);
    cJSON_AddItemToObject(root, "wifi", wifi);

    cJSON *mqtt = cJSON_CreateObject();
    cJSON_AddStringToObject(mqtt, "userLogin", userLogin);
    cJSON_AddItemToObject(root, "mqtt", mqtt);

    cJSON *mac = cJSON_CreateObject();
    cJSON_AddStringToObject(mac, "activeSta", mac_sta_act);
    cJSON_AddStringToObject(mac, "activeAp",  mac_ap_act);
    cJSON_AddStringToObject(mac, "savedSta",  mac_sta_saved);
    cJSON_AddStringToObject(mac, "savedAp",   mac_ap_saved);
    cJSON_AddItemToObject(root, "mac", mac);

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);

    cJSON_free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

/* POST /api/wifi_connect  { ssid, password } */
static esp_err_t handle_post_wifi_connect(httpd_req_t *req) {
    add_cors_headers(req);
    cJSON *root = read_json_body(req);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON"); return ESP_OK; }

    const cJSON *j_ssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON *j_pass = cJSON_GetObjectItem(root, "password");

    char ssid[33]={0}, pass[65]={0};
    if (cJSON_IsString(j_ssid) && j_ssid->valuestring) strncpy(ssid, j_ssid->valuestring, sizeof(ssid)-1);
    if (cJSON_IsString(j_pass) && j_pass->valuestring) strncpy(pass, j_pass->valuestring, sizeof(pass)-1);

    cJSON *resp = cJSON_CreateObject();

    if (!ssid[0]) {
        cJSON_AddStringToObject(resp, "error", "ssid_required");
    } else {
        esp_err_t e = app_nvs_save_wifi(ssid, pass);
        cJSON_AddStringToObject(resp, "wifiSave", e==ESP_OK ? "ok" : esp_err_to_name(e));

        wifi_mgr_err_t w = wifi_mgr_connect_sta(ssid, pass, 10000);
        cJSON_AddStringToObject(resp, "connectResult", (w==WIFI_MGR_OK) ? "ok" : "timeout_or_error");
        cJSON_AddStringToObject(resp, "connectReason", conn_result_to_str(wifi_mgr_get_last_result()));
        cJSON_AddBoolToObject(resp, "connected", wifi_mgr_sta_has_ip());
    }

    char *out = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);

    cJSON_free(out);
    cJSON_Delete(resp);
    cJSON_Delete(root);
    return ESP_OK;
}

/* POST /api/mqtt_user  { userLogin } */
static esp_err_t handle_post_mqtt_user(httpd_req_t *req) {
    add_cors_headers(req);
    cJSON *root = read_json_body(req);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON"); return ESP_OK; }

    const cJSON *j_user = cJSON_GetObjectItem(root, "userLogin");
    char user[65]={0};
    if (cJSON_IsString(j_user) && j_user->valuestring) strncpy(user, j_user->valuestring, sizeof(user)-1);

    cJSON *resp = cJSON_CreateObject();
    if (!user[0]) {
        cJSON_AddBoolToObject(resp, "ok", false);
        cJSON_AddStringToObject(resp, "err", "userLogin_required");
    } else {
        esp_err_t e = app_nvs_save_user_login(user);
        cJSON_AddBoolToObject(resp, "ok", e==ESP_OK);
        if (e != ESP_OK) cJSON_AddStringToObject(resp, "err", esp_err_to_name(e));
    }

    char *out = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);

    cJSON_free(out);
    cJSON_Delete(resp);
    cJSON_Delete(root);
    return ESP_OK;
}

/* POST/PUT /api/mac  { staMac, apMac } — сохраняет MAC и ребутит при успехе */
static esp_err_t handle_post_mac(httpd_req_t *req) {
    add_cors_headers(req);
    cJSON *root = read_json_body(req);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON"); return ESP_OK; }

    const cJSON *j_sta  = cJSON_GetObjectItem(root, "staMac");
    const cJSON *j_ap   = cJSON_GetObjectItem(root, "apMac");

    char macStaStr[18]="", macApStr[18]="";
    if (cJSON_IsString(j_sta) && j_sta->valuestring) strncpy(macStaStr, j_sta->valuestring, sizeof(macStaStr)-1);
    if (cJSON_IsString(j_ap)  && j_ap->valuestring)  strncpy(macApStr,  j_ap->valuestring,  sizeof(macApStr)-1);

    cJSON *resp = cJSON_CreateObject();
    bool ok_any = false;
    bool need_reboot = false;

    /* STA */
    if (macStaStr[0]) {
        uint8_t m[6];
        if (parse_mac_str(macStaStr, m)) {
            esp_err_t e = app_nvs_save_mac_sta(m);
            cJSON_AddStringToObject(resp, "macStaSave", e==ESP_OK ? "ok" : esp_err_to_name(e));
            if (e == ESP_OK) { ok_any = true; need_reboot = true; }
        } else {
            cJSON_AddStringToObject(resp, "macStaSave", "invalid_format_or_bits");
        }
    }

    /* AP */
    if (cJSON_HasObjectItem(root, "apMac")) {
        if (macApStr[0]) {
            uint8_t m[6];
            if (parse_mac_str(macApStr, m)) {
                esp_err_t e = app_nvs_save_mac_ap(m);
                cJSON_AddStringToObject(resp, "macApSave", e==ESP_OK ? "ok" : esp_err_to_name(e));
                if (e == ESP_OK) { ok_any = true; need_reboot = true; }
            } else {
                cJSON_AddStringToObject(resp, "macApSave", "invalid_format_or_bits");
            }
        } else {
            esp_err_t e = app_nvs_save_mac_ap(NULL);  // очистить сохранённый AP MAC
            cJSON_AddStringToObject(resp, "macApSave", e==ESP_OK ? "cleared" : esp_err_to_name(e));
            if (e == ESP_OK) { ok_any = true; need_reboot = true; }
        }
    }

    cJSON_AddBoolToObject(resp, "ok", ok_any);
    cJSON_AddBoolToObject(resp, "reboot", need_reboot);
    cJSON_AddStringToObject(resp, "message", need_reboot ?
        "MAC сохранён. Устройство перезагрузится…" : "Готово");

    char *out = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);

    cJSON_free(out);
    cJSON_Delete(resp);
    cJSON_Delete(root);

    if (need_reboot) {
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    }
    return ESP_OK;
}

/* =================== Запуск/остановка =================== */

esp_err_t web_portal_start(void) {
    if (s_server) return ESP_OK;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.stack_size = 6144; // чуть больше на cJSON
    cfg.max_resp_headers = 10;
    cfg.max_uri_handlers = 16;   // дефолт 8 — может не хватить
    cfg.lru_purge_enable = true;

    esp_err_t e = httpd_start(&s_server, &cfg);
    if (e != ESP_OK) return e;

    /* --- API: OPTIONS --- */
    httpd_register_uri_handler(s_server, &(httpd_uri_t){ .uri="/api/config",       .method=HTTP_OPTIONS, .handler=handle_options });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){ .uri="/api/wifi_connect", .method=HTTP_OPTIONS, .handler=handle_options });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){ .uri="/api/mqtt_user",    .method=HTTP_OPTIONS, .handler=handle_options });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){ .uri="/api/mac",          .method=HTTP_OPTIONS, .handler=handle_options });

    /* --- API: GET/POST/PUT --- */
    httpd_register_uri_handler(s_server, &(httpd_uri_t){ .uri="/api/config",       .method=HTTP_GET,  .handler=handle_get_config });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){ .uri="/api/wifi_connect", .method=HTTP_POST, .handler=handle_post_wifi_connect });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){ .uri="/api/mqtt_user",    .method=HTTP_POST, .handler=handle_post_mqtt_user });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){ .uri="/api/mac",          .method=HTTP_POST, .handler=handle_post_mac });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){ .uri="/api/mac",          .method=HTTP_PUT,  .handler=handle_post_mac });

    /* --- Static (после API!), wildcard --- */
    httpd_register_uri_handler(s_server, &(httpd_uri_t){ .uri="/*", .method=HTTP_GET, .handler=handle_static });

    ESP_LOGI(TAG, "Web portal started on :%u", cfg.server_port);
    return ESP_OK;
}

void web_portal_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
