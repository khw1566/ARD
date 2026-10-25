#pragma once
#include "ard_wifi.h"

// Open setup API. Streaming keeps the HTTP stack independent of AP count.
static esp_err_t scan_json_string(httpd_req_t *req, const char *s) {
    if (httpd_resp_sendstr_chunk(req, "\"") != ESP_OK) return ESP_FAIL;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        char escaped[8];
        if (*p < 32 || *p == '"' || *p == '\\') snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
        else { escaped[0] = *p; escaped[1] = 0; }
        if (httpd_resp_sendstr_chunk(req, escaped) != ESP_OK) return ESP_FAIL;
    }
    return httpd_resp_sendstr_chunk(req, "\"");
}

static esp_err_t scan_page(httpd_req_t *req) {
    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK && !strcmp(query, "refresh=1"))
        ard_wifi_request_scan();
    ard_wifi_ap_t *aps = (ard_wifi_ap_t *)calloc(ARD_WIFI_SCAN_LIMIT, sizeof(*aps));
    if (!aps) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan buffer unavailable");
    bool busy;
    esp_err_t error;
    size_t count = ard_wifi_scan_snapshot(aps, ARD_WIFI_SCAN_LIMIT, &busy, &error);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char text[128];
    snprintf(text, sizeof(text), "{\"busy\":%s,\"error\":%ld,\"networks\":[", busy ? "true" : "false", (long)error);
    esp_err_t err = httpd_resp_sendstr_chunk(req, text);
    for (size_t i = 0; err == ESP_OK && i < count; ++i) {
        err = httpd_resp_sendstr_chunk(req, i ? ",{\"ssid\":" : "{\"ssid\":");
        if (err == ESP_OK) err = scan_json_string(req, aps[i].ssid);
        if (err == ESP_OK) err = httpd_resp_sendstr_chunk(req, ",\"password\":");
        if (err == ESP_OK) err = scan_json_string(req, aps[i].password);
        snprintf(text, sizeof(text), ",\"rssi\":%d,\"open\":%s,\"saved\":%s}", aps[i].rssi,
                 aps[i].open ? "true" : "false", aps[i].saved ? "true" : "false");
        if (err == ESP_OK) err = httpd_resp_sendstr_chunk(req, text);
    }
    free(aps);
    if (err == ESP_OK) err = httpd_resp_sendstr_chunk(req, "]}");
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}
