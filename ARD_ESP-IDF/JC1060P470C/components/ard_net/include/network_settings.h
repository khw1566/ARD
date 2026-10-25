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

// Only a host/IP of 1-63 bytes, never a URL, port, HTML, whitespace or a path.
static inline bool ard_network_valid_server(const char *host) {
    size_t hn = 0;
    while (hn < 64 && host[hn]) {
        char c = host[hn++];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '.' || c == '-')) return false;
    }
    if (!hn || hn > 63) return false;
    return host[0] != '.' && host[0] != '-' && host[hn - 1] != '.' && host[hn - 1] != '-';
}

static inline bool ard_network_valid(const ard_network_settings_t *s) {
    if (s->version != 1 || !memchr(s->ssid, 0, sizeof(s->ssid)) ||
        !memchr(s->password, 0, sizeof(s->password)) ||
        !memchr(s->server, 0, sizeof(s->server))) return false;
    size_t sn = strlen(s->ssid), pn = strlen(s->password);
    if (!sn || sn > 32 || (pn && pn < 8) || pn > 64) return false;
    if (pn == 64) {
        for (size_t i = 0; i < pn; ++i) {
            char c = s->password[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        }
    }
    return ard_network_valid_server(s->server);
}
