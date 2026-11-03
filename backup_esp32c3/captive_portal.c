#include "captive_portal.h"
#include "nvs_config.h"

#include <string.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <lwip/sockets.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>

static const char *TAG = "portal";

static httpd_handle_t s_httpd = NULL;
static TaskHandle_t s_dns_task = NULL;
static int s_dns_socket = -1;

// DNS Header structure
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t num_questions;
    uint16_t num_answers;
    uint16_t num_authorities;
    uint16_t num_additionals;
} dns_header_t;

// Minimal DNS Server task (answers all queries with 192.168.4.1)
static void dns_server_task(void *pvParameters)
{
    uint8_t rx_buffer[256];
    uint8_t tx_buffer[256];
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = INADDR_ANY,
    };

    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        vTaskDelete(NULL);
        return;
    }

    if (bind(s_dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket to port 53");
        close(s_dns_socket);
        s_dns_socket = -1;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS Captive Portal responder started on port 53");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int len = recvfrom(s_dns_socket, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr *)&client_addr, &client_addr_len);

        if (len < (int)sizeof(dns_header_t)) {
            continue;
        }

        dns_header_t *rx_hdr = (dns_header_t *)rx_buffer;
        dns_header_t *tx_hdr = (dns_header_t *)tx_buffer;

        // Copy question to response
        memcpy(tx_buffer, rx_buffer, len);

        tx_hdr->id = rx_hdr->id;
        tx_hdr->flags = htons(0x8180); // Standard response, No error
        tx_hdr->num_questions = rx_hdr->num_questions;
        tx_hdr->num_answers = htons(1);
        tx_hdr->num_authorities = 0;
        tx_hdr->num_additionals = 0;

        int tx_len = len;

        // Answer Record: Name Pointer (0xc00c), Type A (1), Class IN (1), TTL 60s, DataLen 4, IP 192.168.4.1
        const uint8_t answer[] = {
            0xc0, 0x0c,             // Pointer to query name
            0x00, 0x01,             // Type A
            0x00, 0x01,             // Class IN
            0x00, 0x00, 0x00, 0x3c, // TTL 60s
            0x00, 0x04,             // Data length: 4 bytes
            192, 168, 4, 1          // IP Address: 192.168.4.1
        };

        if (tx_len + sizeof(answer) <= sizeof(tx_buffer)) {
            memcpy(tx_buffer + tx_len, answer, sizeof(answer));
            tx_len += sizeof(answer);

            sendto(s_dns_socket, tx_buffer, tx_len, 0,
                   (struct sockaddr *)&client_addr, client_addr_len);
        }
    }
}

// Beautiful, modern mobile-friendly Captive Portal HTML
static const char HTML_PORTAL[] =
"<!DOCTYPE html><html lang='ja'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>SmartPC 初期セットアップ</title>"
"<style>"
":root{--bg:#0f172a;--card:#1e293b;--text:#f8fafc;--primary:#3b82f6;--primary-hover:#2563eb;--input:#334155;--border:#475569;--muted:#94a3b8}"
"*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}"
"body{background:var(--bg);color:var(--text);display:flex;justify-content:center;align-items:center;min-height:100vh;padding:16px}"
".card{background:var(--card);width:100%;max-width:440px;border-radius:16px;box-shadow:0 20px 25px -5px rgba(0,0,0,.5);border:1px solid var(--border);padding:28px}"
".logo{display:flex;align-items:center;gap:12px;margin-bottom:20px}"
".logo svg{width:36px;height:36px;fill:var(--primary)}"
"h1{font-size:22px;font-weight:700}"
".sub{color:var(--muted);font-size:13px;margin-bottom:24px;line-height:1.5}"
".group{margin-bottom:18px}"
"label{display:block;font-size:13px;font-weight:600;margin-bottom:6px;color:#cbd5e1}"
".desc{font-size:11px;color:var(--muted);margin-top:4px}"
"input,select{width:100%;padding:12px;border-radius:8px;border:1px solid var(--border);background:var(--input);color:var(--text);font-size:14px;outline:none;transition:border .2s}"
"input:focus,select:focus{border-color:var(--primary)}"
".btn{width:100%;padding:14px;background:var(--primary);color:#fff;border:none;border-radius:8px;font-size:15px;font-weight:700;cursor:pointer;margin-top:10px;transition:background .2s}"
".btn:hover{background:var(--primary-hover)}"
".scan-btn{background:transparent;border:1px solid var(--primary);color:var(--primary);padding:6px 12px;font-size:12px;border-radius:6px;float:right;cursor:pointer}"
".badge{display:inline-block;padding:2px 8px;border-radius:12px;font-size:11px;background:#065f46;color:#34d399;font-weight:600;margin-left:6px}"
"</style></head><body>"
"<div class='card'>"
"<div class='logo'>"
"<svg viewBox='0 0 24 24'><path d='M4 6h16v10H4zM2 4h20a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zm7 16h6v2H9v-2z'/></svg>"
"<div><h1>SmartPC 設定 <span class='badge'>C3 Edition</span></h1></div>"
"</div>"
"<p class='sub'>Google Home・Matter 連携用の Wi-Fi と、起動対象の PC 情報を登録します。</p>"
"<form action='/save' method='POST'>"
"<div class='group'>"
"<label>Wi-Fi ネットワーク (SSID) <button type='button' class='scan-btn' onclick='scanWifi()'>再スキャン</button></label>"
"<select id='ssid_select' onchange='onSelectSSID(this)'><option value=''>周囲の Wi-Fi を検出中...</option></select>"
"<input type='text' id='ssid_input' name='ssid' placeholder='SSIDを直接入力' style='margin-top:8px;' required>"
"</div>"
"<div class='group'>"
"<label>Wi-Fi パスワード</label>"
"<input type='password' name='pass' placeholder='パスワードを入力'>"
"</div>"
"<div class='group'>"
"<label>PC の MAC アドレス (WoL 起動用)</label>"
"<input type='text' name='mac' placeholder='例: 74:D4:35:89:01:39' pattern='^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$' required>"
"<div class='desc'>※ Windows のコマンドプロンプトで <code>getmac</code> と入力して確認できます。</div>"
"</div>"
"<div class='group'>"
"<label>PC の IP アドレス (稼働監視用 / 省略可)</label>"
"<input type='text' name='ip' placeholder='例: 192.168.11.13' pattern='^([0-9]{1,3}\\.){3}[0-9]{1,3}$'>"
"<div class='desc'>※ Google Home に正確な電源状態を同期するために使用します。</div>"
"</div>"
"<button type='submit' class='btn'>設定を保存して PC をリンク</button>"
"</form></div>"
"<script>"
"function onSelectSSID(s){if(s.value){document.getElementById('ssid_input').value=s.value;}}"
"function scanWifi(){"
"const sel=document.getElementById('ssid_select');"
"sel.innerHTML='<option>スキャン中...</option>';"
"fetch('/scan').then(r=>r.json()).then(d=>{"
"sel.innerHTML='<option value=\"\">-- Wi-Fi を選択 --</option>';"
"d.forEach(ap=>{"
"const o=document.createElement('option');"
"o.value=ap.ssid;o.textContent=ap.ssid+' ('+ap.rssi+' dBm)';sel.appendChild(o);"
"});"
"}).catch(()=>{sel.innerHTML='<option value=\"\">手動で入力してください</option>';});"
"}"
"window.onload=scanWifi;"
"</script></body></html>";

static const char HTML_SUCCESS[] =
"<!DOCTYPE html><html lang='ja'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>保存完了</title>"
"<style>"
"body{background:#0f172a;color:#f8fafc;font-family:sans-serif;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;padding:16px;text-align:center}"
".box{background:#1e293b;border:1px solid #475569;border-radius:16px;padding:32px;max-width:400px}"
"h2{color:#38bdf8;margin-bottom:12px}"
"p{color:#94a3b8;font-size:14px;line-height:1.6}"
"</style></head><body><div class='box'>"
"<h2>設定を保存しました！</h2>"
"<p>SmartPC を再起動し、ご自宅の Wi-Fi に接続しています。<br>数秒後に通常モードへ切り替わります。</p>"
"</div></body></html>";

// URL Decode helper
static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// HTTP Handler: Root / Captive Portal page
static esp_err_t http_root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, HTML_PORTAL, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP Handler: Wi-Fi Scan API (/scan)
static esp_err_t http_scan_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = false
    };

    esp_wifi_scan_start(&scan_config, true);

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 15) ap_count = 15;

    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    char *json = malloc(1024);
    strcpy(json, "[");

    if (ap_records && json) {
        esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        int added = 0;
        for (int i = 0; i < ap_count; i++) {
            if (strlen((char *)ap_records[i].ssid) == 0) continue;
            char item[96];
            snprintf(item, sizeof(item), "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                     (added > 0) ? "," : "", (char *)ap_records[i].ssid, ap_records[i].rssi);
            strcat(json, item);
            added++;
        }
        strcat(json, "]");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send_500(req);
    }

    if (ap_records) free(ap_records);
    if (json) free(json);
    return ESP_OK;
}

static void delayed_restart_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "Rebooting into Station mode...");
    esp_restart();
}

// HTTP Handler: Form Save (/save)
static esp_err_t http_save_handler(httpd_req_t *req)
{
    char buf[512];
    int ret, remaining = req->content_len;
    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    app_config_t cfg = {0};
    cfg.configured = true;

    // Parse form-urlencoded
    char *token = strtok(buf, "&");
    while (token) {
        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            char *key = token;
            char *val = eq + 1;
            char decoded[128];
            url_decode(decoded, val);

            if (strcmp(key, "ssid") == 0) {
                strncpy(cfg.wifi_ssid, decoded, sizeof(cfg.wifi_ssid) - 1);
            } else if (strcmp(key, "pass") == 0) {
                strncpy(cfg.wifi_password, decoded, sizeof(cfg.wifi_password) - 1);
            } else if (strcmp(key, "mac") == 0) {
                strncpy(cfg.pc_mac, decoded, sizeof(cfg.pc_mac) - 1);
            } else if (strcmp(key, "ip") == 0) {
                strncpy(cfg.pc_ip, decoded, sizeof(cfg.pc_ip) - 1);
            }
        }
        token = strtok(NULL, "&");
    }

    ESP_LOGI(TAG, "Saving credentials: SSID='%s', MAC='%s', IP='%s'",
             cfg.wifi_ssid, cfg.pc_mac, cfg.pc_ip);
    nvs_config_save(&cfg);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, HTML_SUCCESS, HTTPD_RESP_USE_STRLEN);

    // Schedule reboot to enter normal operating mode
    xTaskCreate(delayed_restart_task, "restart_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// Redirect all 302 captive portal probe URLs
static esp_err_t http_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t captive_portal_start(void)
{
    ESP_LOGI(TAG, "Starting Captive Portal SoftAP (SSID: SmartPC-Setup)...");

    // Configure Wi-Fi SoftAP mode
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "SmartPC-Setup",
            .ssid_len = strlen("SmartPC-Setup"),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start DNS responder task
    xTaskCreate(dns_server_task, "dns_task", 3072, NULL, 5, &s_dns_task);

    // Start HTTP Server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    if (httpd_start(&s_httpd, &config) == ESP_OK) {
        httpd_uri_t uri_root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = http_root_handler,
        };
        httpd_register_uri_handler(s_httpd, &uri_root);

        httpd_uri_t uri_scan = {
            .uri = "/scan",
            .method = HTTP_GET,
            .handler = http_scan_handler,
        };
        httpd_register_uri_handler(s_httpd, &uri_scan);

        httpd_uri_t uri_save = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = http_save_handler,
        };
        httpd_register_uri_handler(s_httpd, &uri_save);

        // Captive portal probes redirection
        const char *probes[] = {
            "/hotspot-detect.html", // Apple
            "/generate_204",        // Android
            "/ncsi.txt",            // Windows
            "/connecttest.txt"      // Windows
        };
        for (int i = 0; i < 4; i++) {
            httpd_uri_t uri_probe = {
                .uri = probes[i],
                .method = HTTP_GET,
                .handler = http_redirect_handler,
            };
            httpd_register_uri_handler(s_httpd, &uri_probe);
        }
    }

    ESP_LOGI(TAG, "Captive Portal ready at http://192.168.4.1");
    return ESP_OK;
}

void captive_portal_stop(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    if (s_dns_socket >= 0) {
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    if (s_dns_task) {
        vTaskDelete(s_dns_task);
        s_dns_task = NULL;
    }
}
