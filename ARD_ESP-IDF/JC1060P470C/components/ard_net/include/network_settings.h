#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t version;
    char ssid[33];
    char password[65];
    char server[65];
} ard_network_settings_t;

static inline bool ard_network_valid(const ard_network_settings_t *s) {
    if (s->version != 1 || !memchr(s->ssid, 0, sizeof(s->ssid)) ||
        !memchr(s->password, 0, sizeof(s->password)) ||
        !memchr(s->server, 0, sizeof(s->server))) return false;
    size_t sn = strlen(s->ssid), pn = strlen(s->password), hn = strlen(s->server);
    if (!sn || sn > 32 || (pn && pn < 8) || pn > 64 || !hn || hn > 63) return false;
    if (pn == 64) {
        for (size_t i = 0; i < pn; ++i) {
            char c = s->password[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        }
    }
    // Only a host/IP, never a URL, port, HTML, whitespace or a path.
    for (size_t i = 0; i < hn; ++i) {
        char c = s->server[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '.' || c == '-')) return false;
    }
    return s->server[0] != '.' && s->server[0] != '-' &&
           s->server[hn - 1] != '.' && s->server[hn - 1] != '-';
}
