//web_portal.c
#include "web_portal.h"
#include <string.h>
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "cJSON.h"

#include "app_nvs.h"
#include "wifi_manager.h"

static const char *TAG = "web_portal";
static httpd_handle_t s_server = NULL;

/* =================== Вспомогалки =================== */
static void add_cors_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
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

/* =================== HTML страница =================== */
static const char INDEX_HTML[] =
"<!doctype html><html lang='ru'><head><meta charset='utf-8'/>"
"<meta name='viewport' content='width=device-width,initial-scale=1'/>"
"<title>RostClimat — настройка</title>"
"<style>"
"body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,Arial,sans-serif;margin:0;background:#0b1020;color:#e6ecff}"
".wrap{max-width:760px;margin:40px auto;padding:24px;border-radius:16px;background:#121936;box-shadow:0 10px 30px rgba(0,0,0,.4)}"
"h1{font-size:20px;margin:0 0 16px}"
"h2{font-size:16px;margin:18px 0 10px;color:#cfe0ff}"
"label{display:block;margin:12px 0 6px;color:#9fb0ff}"
"input{width:100%;padding:10px 12px;border:1px solid #2a3566;background:#0f1630;color:#e6ecff;border-radius:10px;outline:none}"
"button{margin-top:12px;padding:10px 14px;border:0;border-radius:10px;background:#3b82f6;color:#fff;font-weight:600;cursor:pointer}"
"button:disabled{opacity:.6;cursor:not-allowed}"
".grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px}"
".card{padding:12px;border:1px solid #23305e;border-radius:10px;background:#101838;margin:10px 0}"
".muted{color:#9fb0ff;font-size:12px}.ok{color:#7CFC98}.bad{color:#ff8c7c}"
".msg{margin-top:8px;font-size:14px}"
".hint{font-size:12px;color:#9fb0ff;margin-top:4px}"
".tiny{font-size:12px;opacity:.85}"
".btn-link{background:transparent;color:#9fb0ff;border:0;cursor:pointer;padding:0;margin-left:8px;text-decoration:underline}"
".kv{display:grid;grid-template-columns:auto 1fr;gap:8px 12px;align-items:center;font-size:13px}"
".kv div:nth-child(odd){color:#9fb0ff}"
"</style></head><body><div class='wrap'>"
"<h1>Настройка устройства</h1>"

"<div class='card'>"
" <div class='kv'>"
"  <div>serverId:</div><div><code id='serverId'>—</code></div>"
"  <div>Статус STA:</div><div><span id='staStatus' class='muted'>—</span></div>"
"  <div>Подключен к SSID:</div><div><span id='staSsid' class='muted'>—</span></div>"
"  <div>IP адрес:</div><div><span id='staIp' class='muted'>—</span></div>"
"  <div>Текущий MAC STA:</div><div><span id='macStaAct' class='tiny'>—</span></div>"
"  <div>Текущий MAC AP:</div><div><span id='macApAct' class='tiny'>—</span></div>"
" </div>"
"</div>"

"<div class='card'>"
" <h2>Wi-Fi</h2>"
" <div class='grid2'>"
"  <div><label>SSID</label><input id='ssid' placeholder='HomeWiFi'/></div>"
"  <div><label>Пароль</label>"
"   <input id='password' type='password' placeholder='••••••••'/>"
"   <button class='btn-link' id='togglePwd'>показать/скрыть</button>"
"  </div>"
" </div>"
" <button id='btnWifi'>Подключиться</button>"
" <div id='msgWifi' class='msg muted'></div>"
"</div>"

"<div class='card'>"
" <h2>MQTT</h2>"
" <label>userLogin</label><input id='userLogin' placeholder='user123'/>"
" <button id='btnUser'>Сохранить MQTT userLogin</button>"
" <div id='msgUser' class='msg muted'></div>"
"</div>"

"<div class='card'>"
" <h2>MAC адреса</h2>"
" <div class='grid2'>"
"  <div><label>Пользовательский MAC STA</label>"
"   <input id='macSta' placeholder='02:11:22:33:44:55'/>"
"   <div class='hint'>Должен быть unicast &amp; locally administered (бит0=0, бит1=1).</div>"
"  </div>"
"  <div><label>Пользовательский MAC AP</label>"
"   <input id='macAp' placeholder='02:11:22:33:44:56'/>"
"   <div class='hint'>Оставьте пустым — вернётся к автогенерации от STA.</div>"
"  </div>"
" </div>"
" <button id='btnMac'>Сохранить MAC</button>"
" <div id='msgMac' class='msg muted'></div>"
"</div>"

"<script>"
"const macRe=/^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$/;"
"document.getElementById('togglePwd').onclick=function(e){e.preventDefault();const p=document.getElementById('password');p.type=p.type==='password'?'text':'password';};"

"async function load(){"
"  try{const r=await fetch('/api/config');const j=await r.json();"
"    document.getElementById('serverId').textContent=j.server?.serverId||'—';"
"    document.getElementById('ssid').value=j.wifi?.ssid||'';"
"    document.getElementById('userLogin').value=j.mqtt?.userLogin||'';"
"    document.getElementById('staStatus').textContent=j.wifi?.connected?'Подключен':'Не подключен';"
"    document.getElementById('staStatus').className=j.wifi?.connected?'ok':'bad';"
"    document.getElementById('staSsid').textContent=j.wifi?.staSsid||'—';"
"    document.getElementById('staIp').textContent=j.wifi?.ip||'—';"
"    document.getElementById('macStaAct').textContent=j.mac?.activeSta||'—';"
"    document.getElementById('macApAct').textContent=j.mac?.activeAp||'—';"
"    document.getElementById('macSta').value=j.mac?.savedSta||'';"
"    document.getElementById('macAp').value=j.mac?.savedAp||'';"
"  }catch(e){console.error(e);}"
"}"

"async function wifiConnect(){"
"  const btn=document.getElementById('btnWifi');btn.disabled=true;"
"  const m=document.getElementById('msgWifi');m.textContent='';"
"  const ssid=document.getElementById('ssid').value.trim();"
"  const password=document.getElementById('password').value;"
"  if(!ssid){m.textContent='Укажите SSID';btn.disabled=false;return;}"
"  try{"
"    const r=await fetch('/api/wifi_connect',{method:'POST',headers:{'Content-Type':'application/json'},"
"      body:JSON.stringify({ssid,password})});"
"    const j=await r.json();"
"    m.textContent=(j.connected?'OK: IP получен':'Не удалось подключиться')+(j.connectReason?(' ('+j.connectReason+')'):'');"
"    await load();"
"  }catch(e){m.textContent='Ошибка: '+e;}"
"  finally{btn.disabled=false;}"
"}"

"async function saveUser(){"
"  const btn=document.getElementById('btnUser');btn.disabled=true;"
"  const m=document.getElementById('msgUser');m.textContent='';"
"  const userLogin=document.getElementById('userLogin').value.trim();"
"  if(!userLogin){m.textContent='Укажите userLogin';btn.disabled=false;return;}"
"  try{"
"    const r=await fetch('/api/mqtt_user',{method:'POST',headers:{'Content-Type':'application/json'},"
"      body:JSON.stringify({userLogin})});"
"    const j=await r.json();"
"    m.textContent=(j.ok?'Сохранено':'Ошибка')+(j.err?(' '+j.err):'');"
"  }catch(e){m.textContent='Ошибка: '+e;}"
"  finally{btn.disabled=false;}"
"}"

"async function saveMac(){"
"  const btn=document.getElementById('btnMac');btn.disabled=true;"
"  const m=document.getElementById('msgMac');m.textContent='';"
"  const macSta=document.getElementById('macSta').value.trim();"
"  const macAp=document.getElementById('macAp').value.trim();"
"  if(macSta && !macRe.test(macSta)){m.textContent='Неверный формат MAC STA';btn.disabled=false;return;}"
"  if(macAp && !macRe.test(macAp)){m.textContent='Неверный формат MAC AP';btn.disabled=false;return;}"
"  try{"
"    const r=await fetch('/api/mac',{method:'POST',headers:{'Content-Type':'application/json'},"
"      body:JSON.stringify({staMac:macSta,apMac:macAp})});"
"    const j=await r.json();"
"    m.textContent=j.message||'OK';"
"    if(j.reboot){m.textContent+=' Перезагрузка…';}"
"  }catch(e){m.textContent='Ошибка: '+e;}"
"  finally{btn.disabled=false;}"
"}"

"document.getElementById('btnWifi').addEventListener('click',wifiConnect);"
"document.getElementById('btnUser').addEventListener('click',saveUser);"
"document.getElementById('btnMac').addEventListener('click',saveMac);"
"load();"
"</script></div></body></html>";

/* =================== JSON helpers =================== */
static cJSON* read_json_body(httpd_req_t *req) {
    int total = req->content_len;
    if (total <= 0 || total > 4096) return NULL;
    char *buf = (char*) malloc(total + 1);
    if (!buf) return NULL;
    int rcv = httpd_req_recv(req, buf, total);
    if (rcv <= 0) { free(buf); return NULL; }
    buf[total] = '\0';
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

/* =================== Handlers =================== */

static esp_err_t handle_index(httpd_req_t *req) {
    add_cors_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_options(httpd_req_t *req) {
    add_cors_headers(req);
    return httpd_resp_send(req, NULL, 0);
}

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

/* POST /api/mac  { staMac, apMac } — сохраняет MAC и ребутит при успехе */
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

    esp_err_t e = httpd_start(&s_server, &cfg);
    if (e != ESP_OK) return e;

    httpd_register_uri_handler(s_server, &(httpd_uri_t){
        .uri="/", .method=HTTP_GET, .handler=handle_index });

    httpd_register_uri_handler(s_server, &(httpd_uri_t){
        .uri="/api/config", .method=HTTP_OPTIONS, .handler=handle_options });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){
        .uri="/api/wifi_connect", .method=HTTP_OPTIONS, .handler=handle_options });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){
        .uri="/api/mqtt_user", .method=HTTP_OPTIONS, .handler=handle_options });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){
        .uri="/api/mac", .method=HTTP_OPTIONS, .handler=handle_options });

    httpd_register_uri_handler(s_server, &(httpd_uri_t){
        .uri="/api/config", .method=HTTP_GET, .handler=handle_get_config });

    httpd_register_uri_handler(s_server, &(httpd_uri_t){
        .uri="/api/wifi_connect", .method=HTTP_POST, .handler=handle_post_wifi_connect });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){
        .uri="/api/mqtt_user", .method=HTTP_POST, .handler=handle_post_mqtt_user });
    httpd_register_uri_handler(s_server, &(httpd_uri_t){
        .uri="/api/mac", .method=HTTP_POST, .handler=handle_post_mac });

    ESP_LOGI(TAG, "Web portal started on :%u", cfg.server_port);
    return ESP_OK;
}

void web_portal_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
