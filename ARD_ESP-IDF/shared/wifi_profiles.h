#pragma once
#include "network_settings.h"

#define ARD_WIFI_SLOTS 8
typedef struct { char ssid[33], password[65]; } ard_wifi_profile_t;
typedef struct {
    uint32_t version, count;
    ard_wifi_profile_t entries[ARD_WIFI_SLOTS]; // Most recent success first.
} ard_wifi_profiles_t;

static inline bool ard_wifi_profile_valid(const ard_wifi_profile_t *p) {
    ard_network_settings_t s = {0};
    s.version = 1;
    memcpy(s.ssid, p->ssid, sizeof(s.ssid));
    memcpy(s.password, p->password, sizeof(s.password));
    strcpy(s.server, "pc.local");
    return ard_network_valid(&s);
}

static inline bool ard_wifi_profiles_valid(const ard_wifi_profiles_t *p) {
    if (p->version != 1 || p->count > ARD_WIFI_SLOTS) return false;
    for (uint32_t i = 0; i < p->count; ++i) {
        if (!ard_wifi_profile_valid(&p->entries[i])) return false;
        for (uint32_t j = 0; j < i; ++j)
            if (!strcmp(p->entries[i].ssid, p->entries[j].ssid)) return false;
    }
    return true;
}

// Called only after DHCP succeeds. A changed password replaces the same SSID.
static inline bool ard_wifi_profiles_remember(ard_wifi_profiles_t *p, const ard_wifi_profile_t *success) {
    if (!ard_wifi_profile_valid(success)) return false;
    ard_wifi_profile_t copy = *success;
    uint32_t i = 0;
    while (i < p->count && strcmp(p->entries[i].ssid, copy.ssid)) ++i;
    if (i == 0 && p->count && !strcmp(p->entries[0].password, copy.password)) return false;
    if (i == p->count) {
        if (p->count < ARD_WIFI_SLOTS) ++p->count;
        else i = ARD_WIFI_SLOTS - 1;
    }
    memmove(p->entries + 1, p->entries, i * sizeof(*p->entries));
    p->entries[0] = copy;
    return true;
}
