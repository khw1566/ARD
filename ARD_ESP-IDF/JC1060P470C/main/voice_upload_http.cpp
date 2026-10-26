#include "voice_upload_http.h"
#include "visitor_voice.h"
#include "ard_network.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

namespace {
esp_err_t answer(httpd_req_t *req, esp_err_t error, size_t length, const char *digest) {
    if (error != ESP_OK) {
        httpd_resp_set_status(req, error == ESP_ERR_TIMEOUT ? "409 Conflict" :
            error == ESP_ERR_INVALID_STATE ? "503 Service Unavailable" :
            error == ESP_ERR_NOT_FOUND ? "404 Not Found" : "400 Bad Request");
        return httpd_resp_sendstr(req, esp_err_to_name(error));
    }
    char body[160];
    snprintf(body, sizeof(body), "{\"bytes\":%u,\"sha256\":\"%s\"}", static_cast<unsigned>(length), digest);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}
bool authorized(httpd_req_t *req) {
    char token[33];
    return httpd_req_get_hdr_value_str(req, "X-Ard-Token", token, sizeof(token)) == ESP_OK && !strcmp(token, ard_network_token());
}
bool identify(httpd_req_t *req, VisitorVoice *id, bool *korean) {
    char query[128], lang[4], name[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "lang", lang, sizeof(lang)) != ESP_OK ||
        httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK ||
        (strcmp(lang, "ko") && strcmp(lang, "en"))) return false;
    for (unsigned i = 0; i < static_cast<unsigned>(VisitorVoice::Count); ++i) {
        if (!strcmp(name, visitor_voice_name(static_cast<VisitorVoice>(i)))) {
            *id = static_cast<VisitorVoice>(i); *korean = !strcmp(lang, "ko"); return true;
        }
    }
    return false;
}
esp_err_t status(httpd_req_t *req) {
    char body[160];
    snprintf(body, sizeof(body), "{\"protocol\":1,\"sd_ready\":%s,\"token\":\"%s\"}",
             visitor_voice_sd_ready() ? "true" : "false", ard_network_token());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}
esp_err_t hash(httpd_req_t *req) {
    if (!authorized(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid token");
    VisitorVoice id; bool korean;
    if (!identify(req, &id, &korean)) return answer(req, ESP_ERR_INVALID_ARG, 0, "");
    char digest[65]{}; size_t length = 0;
    const esp_err_t error = visitor_voice_hash(id, korean, &length, digest);
    return answer(req, error, length, digest);
}
esp_err_t upload(httpd_req_t *req) {
    if (!authorized(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid token");
    VisitorVoice id; bool korean;
    char expected[65];
    if (!identify(req, &id, &korean) || req->content_len < 44 || req->content_len > 512 * 1024 + 4096 ||
        httpd_req_get_hdr_value_str(req, "X-SHA256", expected, sizeof(expected)) != ESP_OK || strlen(expected) != 64)
        return answer(req, ESP_ERR_INVALID_ARG, 0, "");
    for (const char c : expected) if (c && !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        return answer(req, ESP_ERR_INVALID_ARG, 0, "");
    if (!visitor_voice_sd_ready()) return answer(req, ESP_ERR_INVALID_STATE, 0, "");
    auto *body = static_cast<uint8_t *>(heap_caps_malloc(req->content_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) return answer(req, ESP_ERR_NO_MEM, 0, "");
    const int64_t deadline = esp_timer_get_time() + 20000000;
    size_t received = 0;
    while (received < req->content_len && esp_timer_get_time() < deadline) {
        const int n = httpd_req_recv(req, reinterpret_cast<char *>(body + received), req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) break;
        received += n;
    }
    char digest[65]{};
    const esp_err_t result = received == req->content_len ?
        visitor_voice_store(id, korean, body, received, expected, digest) : ESP_ERR_INVALID_SIZE;
    heap_caps_free(body);
    return answer(req, result, received, digest);
}
}

esp_err_t voice_upload_http_register(httpd_handle_t server) {
    httpd_uri_t route{};
    route.uri = "/api/voice/status"; route.method = HTTP_GET; route.handler = status;
    esp_err_t err = httpd_register_uri_handler(server, &route);
    if (err != ESP_OK) return err;
    route.uri = "/api/voice/file"; route.method = HTTP_GET; route.handler = hash;
    err = httpd_register_uri_handler(server, &route);
    if (err != ESP_OK) return err;
    route.method = HTTP_POST; route.handler = upload;
    return httpd_register_uri_handler(server, &route);
}
