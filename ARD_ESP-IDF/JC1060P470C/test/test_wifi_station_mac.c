#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_station_mac_test_sdk.h"
#include "../components/ard_net/wifi_station_mac.c"

static esp_netif_t sta;
static const uint8_t valid_mac[6] = {0x24, 0x6f, 0x28, 0x11, 0x22, 0x33};
static uint8_t radio_mac[6];
static bool missing_netif, ignore_set;
static int radio_calls, get_calls, set_calls;
static esp_err_t radio_error, initial_get_error, set_error, readback_error;
static unsigned cases;

void mac_test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
    assert(!strcmp(key, "WIFI_STA_DEF"));
    return missing_netif ? NULL : &sta;
}
esp_err_t esp_wifi_get_mac(int iface, uint8_t *out) {
    assert(iface == WIFI_IF_STA); ++radio_calls;
    if (radio_error) return radio_error;
    memcpy(out, radio_mac, 6); return ESP_OK;
}
esp_err_t esp_netif_get_mac(esp_netif_t *netif, uint8_t *out) {
    assert(netif == &sta); ++get_calls;
    const esp_err_t err = get_calls == 1 ? initial_get_error : readback_error;
    if (err) return err;
    memcpy(out, sta.mac, 6); return ESP_OK;
}
esp_err_t esp_netif_set_mac(esp_netif_t *netif, uint8_t *mac) {
    assert(netif == &sta && !sta.up); /* Never mutate a live interface. */
    assert(!memcmp(mac, radio_mac, 6)); ++set_calls;
    if (set_error) return set_error;
    if (!ignore_set) memcpy(sta.mac, mac, 6);
    return ESP_OK;
}
bool esp_netif_is_netif_up(esp_netif_t *netif) { assert(netif == &sta); return sta.up; }

static void reset(void) {
    memset(&sta, 0, sizeof(sta)); memcpy(radio_mac, valid_mac, 6);
    missing_netif = ignore_set = false;
    radio_calls = get_calls = set_calls = 0;
    radio_error = initial_get_error = set_error = readback_error = ESP_OK;
    ++cases;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    reset();
    assert(ard_wifi_sync_station_mac() == ESP_OK);
    assert(radio_calls == 1 && get_calls == 2 && set_calls == 1);
    assert(!memcmp(sta.mac, valid_mac, 6)); /* Cached zero identity repaired. */

    reset(); memcpy(sta.mac, valid_mac, 6);
    assert(ard_wifi_sync_station_mac() == ESP_OK && set_calls == 0 && get_calls == 1);
    reset(); memcpy(sta.mac, valid_mac, 6); sta.up = true;
    assert(ard_wifi_sync_station_mac() == ESP_OK && set_calls == 0);
    reset(); sta.up = true;
    assert(ard_wifi_sync_station_mac() == ESP_ERR_INVALID_STATE && set_calls == 0);
    assert(sta.mac[0] == 0); /* Mismatch is reported without mutating active DHCP. */

    reset(); memset(radio_mac, 0, 6);
    assert(ard_wifi_sync_station_mac() == ESP_ERR_INVALID_ARG && get_calls == 0 && set_calls == 0);
    reset(); radio_mac[0] |= 1;
    assert(ard_wifi_sync_station_mac() == ESP_ERR_INVALID_ARG && set_calls == 0);
    reset(); memset(radio_mac, 255, 6);
    assert(ard_wifi_sync_station_mac() == ESP_ERR_INVALID_ARG && set_calls == 0);
    reset(); radio_mac[0] = 2;
    assert(ard_wifi_sync_station_mac() == ESP_OK && sta.mac[0] == 2); /* Local unicast accepted. */

    reset(); missing_netif = true;
    assert(ard_wifi_sync_station_mac() == ESP_ERR_INVALID_STATE && radio_calls == 0 && set_calls == 0);
    reset(); radio_error = -10;
    assert(ard_wifi_sync_station_mac() == -10 && get_calls == 0 && set_calls == 0);
    reset(); initial_get_error = -11;
    assert(ard_wifi_sync_station_mac() == -11 && set_calls == 0);
    reset(); set_error = -12;
    assert(ard_wifi_sync_station_mac() == -12 && get_calls == 1 && sta.mac[0] == 0);
    reset(); readback_error = -13;
    assert(ard_wifi_sync_station_mac() == -13 && get_calls == 2 && set_calls == 1);
    reset(); ignore_set = true;
    assert(ard_wifi_sync_station_mac() == ESP_FAIL && get_calls == 2 && set_calls == 1);
    printf("Wi-Fi station MAC regression: %u cases passed\n", cases);
    return 0;
}
