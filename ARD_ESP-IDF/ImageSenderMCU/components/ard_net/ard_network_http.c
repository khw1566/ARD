#include "ard_network.h"
#include "portal_page.h"
#include "form_decode.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"

#include "ard_wifi_http.h"

static esp_err_t send_escaped(httpd_req_t *req, const char *text) {
    for (; *text; ++text) {
        const char *escaped = NULL;
        switch (*text) {
        case '&': escaped = "&amp;"; break; case '<': escaped = "&lt;"; break;
        case '>': escaped = "&gt;"; break; case '\'': escaped = "&#39;"; break;
        case '"': escaped = "&quot;"; break;
        }
        esp_err_t err = httpd_resp_send_chunk(req, escaped ? escaped : text, escaped ? strlen(escaped) : 1);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
static esp_err_t page(httpd_req_t *req) {
    const ard_network_settings_t *settings = ard_network_settings();
    const char *keys[] = {"{{HOST}}", "{{STATUS}}", "{{TOKEN}}", "{{SSID}}", "{{SERVER}}", "{{PASSWORD}}"};
    const char *values[] = {ard_network_hostname(), ard_network_connected() ? "LAN / Wi-Fi 연결됨" : "Wi-Fi 미연결 · 설정 AP 사용 가능",
                            ard_network_token(), settings->ssid, settings->server, settings->password};
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const char *cursor = ARD_PORTAL_PAGE;
    while (*cursor) {
        const char *next = strstr(cursor, "{{");
        if (!next) { if (httpd_resp_sendstr_chunk(req, cursor) != ESP_OK) return ESP_FAIL; break; }
        if (httpd_resp_send_chunk(req, cursor, next - cursor) != ESP_OK) return ESP_FAIL;
        bool matched = false;
        for (int i = 0; i < 6; ++i) {
            if (!strncmp(next, keys[i], strlen(keys[i]))) {
                if (send_escaped(req, values[i]) != ESP_OK) return ESP_FAIL;
                cursor = next + strlen(keys[i]); matched = true; break;
            }
        }
        if (!matched) return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t save(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len >= 768) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form length");
    char body[768], token[33];
    size_t offset = 0;
    while (offset < req->content_len) {
        int read = httpd_req_recv(req, body + offset, req->content_len - offset);
        if (read <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete form");
        offset += read;
    }
    body[offset] = 0;
    if (strlen(body) != offset) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form");
    if (!ard_form_field(body, "token", token, sizeof(token)) || strcmp(token, ard_network_token()))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid form token");
    ard_network_settings_t next = {.version = 1};
    if (!ard_form_field(body, "ssid", next.ssid, sizeof(next.ssid)) || !ard_form_field(body, "password", next.password, sizeof(next.password)) ||
        !ard_form_field(body, "server", next.server, sizeof(next.server)) || !ard_network_valid(&next))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID/password/server host");
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ard-network", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS open failed");
    err = nvs_set_blob(nvs, "settings", &next, sizeof(next));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS save failed");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    err = httpd_resp_sendstr(req, "<meta charset='utf-8'><p>저장했습니다. 보드가 재시작됩니다. 같은 설정 AP 또는 .local 주소로 다시 접속하세요.</p>");
    ard_network_restart_later();
    return err;
}

esp_err_t ard_network_register_http(httpd_handle_t server) {
    httpd_uri_t get = {.uri = "/network", .method = HTTP_GET, .handler = page};
    httpd_uri_t post = {.uri = "/network/save", .method = HTTP_POST, .handler = save};
    httpd_uri_t scan = {0};
    scan.uri = "/network/scan"; scan.method = HTTP_GET; scan.handler = scan_page;
    esp_err_t scan_err = httpd_register_uri_handler(server, &scan);
    if (scan_err != ESP_OK) return scan_err;
    esp_err_t err = httpd_register_uri_handler(server, &get);
    return err == ESP_OK ? httpd_register_uri_handler(server, &post) : err;
}
