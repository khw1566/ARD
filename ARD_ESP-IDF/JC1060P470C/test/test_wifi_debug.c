#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_debug_test_sdk.h"
/* Exercise the production wrappers with fake netif/driver boundaries. */
#include "../components/ard_net/wifi_debug.c"

static int real_set_calls, normal_calls, wrap_calls, receive_calls, release_calls, replacement_calls;
static esp_err_t set_result, transmit_result, receive_result;
static void *expected_handle, *expected_buffer, *expected_reference;
static size_t expected_length;
static int64_t clock_us;
static bool mutate_receive;
static int handle_a, handle_b, reference, event_buffer;
static uint8_t packet[286];

void fake_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
int64_t esp_timer_get_time(void) { return clock_us; }
const char *esp_netif_get_ifkey(esp_netif_t *netif) { return netif->key; }
esp_err_t esp_netif_dhcpc_get_status(esp_netif_t *netif, esp_netif_dhcp_status_t *state) {
    (void)netif; *state = ESP_NETIF_DHCP_STARTED; return ESP_OK;
}
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *ip) {
    (void)netif; memset(ip, 0, sizeof(*ip)); return ESP_OK;
}
esp_err_t esp_netif_get_mac(esp_netif_t *netif, uint8_t *mac) {
    (void)netif; memset(mac, 0x42, 6); return ESP_OK;
}
bool esp_netif_is_netif_up(esp_netif_t *netif) { (void)netif; return true; }
const esp_app_desc_t *esp_app_get_description(void) {
    static const esp_app_desc_t app = {"test", "test", "date", "time", "idf"};
    return &app;
}

esp_err_t __real_esp_netif_set_driver_config(esp_netif_t *netif, const esp_netif_driver_ifconfig_t *config) {
    ++real_set_calls;
    if (set_result != ESP_OK) return set_result;
    if (!netif || !config) return ESP_FAIL;
    netif->driver = *config;
    return ESP_OK;
}

esp_err_t __real_esp_netif_receive(esp_netif_t *netif, void *buffer, size_t length, void *eb) {
    assert(netif && buffer == expected_buffer && length == expected_length && eb == &event_buffer);
    ++receive_calls;
    if (mutate_receive) memset(buffer, 0, length); /* Real receiver can consume/free the buffer. */
    return receive_result;
}

static esp_err_t original_transmit(void *handle, void *buffer, size_t length) {
    assert(handle == expected_handle && buffer == expected_buffer && length == expected_length);
    ++normal_calls;
    return transmit_result;
}

static esp_err_t original_transmit_wrap(void *handle, void *buffer, size_t length, void *netstack) {
    assert(handle == expected_handle && buffer == expected_buffer && length == expected_length);
    assert(netstack == expected_reference);
    ++wrap_calls;
    return transmit_result;
}

static esp_err_t replacement_transmit(void *handle, void *buffer, size_t length) {
    (void)handle; (void)buffer; (void)length;
    ++replacement_calls;
    return ESP_FAIL;
}

static void original_free(void *handle, void *buffer) {
    assert(handle == expected_handle && buffer == expected_buffer);
    ++release_calls;
}

static esp_err_t original_mac_filter(void *handle, const uint8_t *mac, size_t length, bool add) {
    (void)handle; (void)mac; (void)length; (void)add;
    return ESP_OK;
}

static void discover(void) {
    memset(packet, 0, sizeof(packet));
    packet[12] = 8; packet[14] = 0x45;
    packet[16] = 1; packet[17] = 16; packet[23] = 17; /* IPv4 total length 272. */
    packet[35] = 68; packet[37] = 67; packet[39] = 252; /* UDP length 252. */
    packet[42] = 1; packet[43] = 1; packet[44] = 6;
    packet[46] = 0x12; packet[47] = 0x34; packet[48] = 0x56; packet[49] = 0x78;
    packet[278] = 99; packet[279] = 130; packet[280] = 83; packet[281] = 99;
    packet[282] = 53; packet[283] = 1; packet[284] = 1; packet[285] = 255;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    esp_netif_t sta = {.key = "WIFI_STA_DEF"}, ap = {.key = "WIFI_AP_DEF"};
    esp_netif_t second_sta = {.key = "WIFI_STA_DEF"};
    const esp_netif_driver_ifconfig_t config = {
        .handle = &handle_a, .transmit = original_transmit,
        .transmit_wrap = original_transmit_wrap, .driver_free_rx_buffer = original_free,
        .driver_set_mac_filter = original_mac_filter};
    const esp_netif_driver_ifconfig_t saved = config;
    assert(__wrap_esp_netif_set_driver_config(&sta, &config) == ESP_OK);
    assert(real_set_calls == 1 && station == &sta);
    assert(sta.driver.handle == config.handle && sta.driver.transmit != config.transmit);
    assert(sta.driver.transmit_wrap != config.transmit_wrap);
    assert(sta.driver.driver_free_rx_buffer == config.driver_free_rx_buffer);
    assert(sta.driver.driver_set_mac_filter == config.driver_set_mac_filter);
    assert(!memcmp(&config, &saved, sizeof(config))); /* Caller-owned config untouched. */
    expected_handle = &handle_a; expected_buffer = packet; expected_length = sizeof(packet);
    expected_reference = &reference;
    discover(); transmit_result = -7;
    assert(sta.driver.transmit(sta.driver.handle, packet, sizeof(packet)) == -7);
    assert(normal_calls == 1 && wrap_calls == 0 && tx.packets == 1 && tx.errors == 1);
    assert(tx.dhcp == 1 && tx.type == 1 && tx.xid == 0x12345678);
    transmit_result = ESP_OK;
    assert(sta.driver.transmit_wrap(sta.driver.handle, packet, sizeof(packet), &reference) == ESP_OK);
    assert(normal_calls == 1 && wrap_calls == 1 && tx.packets == 2 && tx.dhcp == 2);
    sta.driver.driver_free_rx_buffer(sta.driver.handle, packet);
    assert(release_calls == 1);

    assert(__wrap_esp_netif_set_driver_config(&ap, &config) == ESP_OK);
    assert(ap.driver.transmit == config.transmit && ap.driver.transmit_wrap == config.transmit_wrap);
    assert(station == &sta);
    assert(ap.driver.transmit(ap.driver.handle, packet, sizeof(packet)) == ESP_OK);
    assert(normal_calls == 2 && tx.packets == 2); /* AP traffic is not counted. */
    receive_result = -8; mutate_receive = true;
    assert(__wrap_esp_netif_receive(&sta, packet, sizeof(packet), &event_buffer) == -8);
    assert(receive_calls == 1 && rx.packets == 1 && rx.errors == 1);
    assert(rx.dhcp == 1 && rx.type == 1 && rx.xid == 0x12345678 && packet[14] == 0);
    discover(); receive_result = ESP_OK;
    assert(__wrap_esp_netif_receive(&ap, packet, sizeof(packet), &event_buffer) == ESP_OK);
    assert(receive_calls == 2 && rx.packets == 1); /* AP receive still delegated. */

    esp_netif_driver_ifconfig_t replacement = config;
    replacement.handle = &handle_b; replacement.transmit = replacement_transmit;
    set_result = -9;
    assert(__wrap_esp_netif_set_driver_config(&second_sta, &replacement) == -9);
    assert(station == &sta && original.transmit == original_transmit && original.handle == &handle_a);
    discover();
    assert(sta.driver.transmit(sta.driver.handle, packet, sizeof(packet)) == ESP_OK);
    assert(normal_calls == 3 && replacement_calls == 0 && tx.packets == 3);
    assert(__wrap_esp_netif_receive(&second_sta, packet, sizeof(packet), &event_buffer) == ESP_OK);
    assert(rx.packets == 1); /* Failed setter did not change the traced RX interface. */
    set_result = ESP_OK;
    esp_netif_driver_ifconfig_t traced = sta.driver;
    assert(__wrap_esp_netif_set_driver_config(&sta, &traced) == ESP_OK);
    discover();
    assert(sta.driver.transmit(sta.driver.handle, packet, sizeof(packet)) == ESP_OK);
    assert(normal_calls == 4 && tx.packets == 4); /* Reinstall must not recurse. */
    transmit_result = -10;
    assert(sta.driver.transmit_wrap(sta.driver.handle, packet, sizeof(packet), &reference) == -10);
    assert(wrap_calls == 2 && tx.packets == 5 && tx.errors == 2 && tx.last_error == -10);
    assert(__wrap_esp_netif_set_driver_config(NULL, &config) == ESP_FAIL);
    assert(__wrap_esp_netif_set_driver_config(&sta, NULL) == ESP_FAIL);
    wifi_debug_init(); wifi_debug_dump("test"); wifi_debug_poll("test");
    clock_us = 6000000; wifi_debug_poll("test");
    printf("Wi-Fi debug boundary tests passed: STA/AP, TX, TX-wrap, RX consumption, rollback, reinstall\n");
    return 0;
}
