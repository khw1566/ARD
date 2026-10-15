#include "network_settings.h"
#include "form_decode.h"
#include <assert.h>
#include <stdio.h>

int main() {
    ard_network_settings_t s = {1, "Lab WiFi", "password123", "192.168.0.35"};
    assert(ard_network_valid(&s));
    s.password[0] = 0; assert(ard_network_valid(&s)); // Open network.
    strcpy(s.password, "short"); assert(!ard_network_valid(&s));
    memset(s.password, 'a', 64); s.password[64] = 0; assert(ard_network_valid(&s));
    s.password[0] = 'z'; assert(!ard_network_valid(&s));
    strcpy(s.password, "password123");
    strcpy(s.server, "pc-name.local"); assert(ard_network_valid(&s));
    strcpy(s.server, "http://pc.local:5000"); assert(!ard_network_valid(&s));
    strcpy(s.server, "pc.local");
    memset(s.ssid, 's', 32); s.ssid[32] = 0; assert(ard_network_valid(&s));
    s.ssid[32] = 'x'; assert(!ard_network_valid(&s)); // Corrupt NVS blob.
    strcpy(s.ssid, ""); assert(!ard_network_valid(&s));
    strcpy(s.ssid, "Lab"); s.version = 2; assert(!ard_network_valid(&s));
    char out[33];
    assert(ard_form_field("ssid=Lab+WiFi&password=a%26b%2Bc%3D", "ssid", out, sizeof(out)) && !strcmp(out, "Lab WiFi"));
    assert(ard_form_field("ssid=Lab&password=a%26b%2Bc%3D", "password", out, sizeof(out)) && !strcmp(out, "a&b+c="));
    assert(ard_form_field("password=&ssid=Lab", "password", out, sizeof(out)) && !out[0]);
    assert(!ard_form_field("ssid=a&ssid=b", "ssid", out, sizeof(out)));
    assert(!ard_form_field("ssid=%00", "ssid", out, sizeof(out)));
    assert(!ard_form_field("ssid=%", "ssid", out, sizeof(out)));
    assert(!ard_form_field("ssid=%0x", "ssid", out, sizeof(out)));
    assert(!ard_form_field("password=Lab", "ssid", out, sizeof(out)));
    assert(!ard_form_field("ssid=abcdefghijklmnopqrstuvwxyz1234567", "ssid", out, sizeof(out)));
    assert(ard_form_field("ssid=abcdefghijklmnopqrstuvwxyz123456", "ssid", out, sizeof(out)));
    assert(ard_form_field("ssid=%ED%95%9C%EA%B8%80", "ssid", out, sizeof(out)) && !strcmp(out, "한글"));
    puts("PASS: NVS validation, open/WPA PSK, field bounds, URL decoding and UTF-8");
}
