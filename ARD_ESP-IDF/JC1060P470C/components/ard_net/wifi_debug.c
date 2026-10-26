// Passive STA data-path diagnostics. Never retain, alter or free packet buffers.
#include "wifi_debug.h"
#if CONFIG_ARD_WIFI_DIAGNOSTICS
#include <inttypes.h>
#include <string.h>
#include "wifi_dhcp_frame.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "WIFI_DEBUG";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static esp_netif_t *station;
static esp_netif_driver_ifconfig_t original;
static int64_t next_dump;
typedef struct {
    uint32_t packets, bytes, errors, dhcp, arp, xid;
    uint8_t type;
    esp_err_t last_error;
} traffic_t;
static traffic_t tx, rx;

esp_err_t __real_esp_netif_set_driver_config(esp_netif_t *, const esp_netif_driver_ifconfig_t *);
esp_err_t __real_esp_netif_receive(esp_netif_t *, void *, size_t, void *);

static const char *dhcp_type(uint8_t type) {
    switch (type) {
    case 1: return "DISCOVER";
    case 2: return "OFFER";
    case 3: return "REQUEST";
    case 4: return "DECLINE";
    case 5: return "ACK";
    case 6: return "NAK";
    case 7: return "RELEASE";
    case 8: return "INFORM";
    default: return "unknown";
    }
}

static void count(traffic_t *target, size_t len, ard_wifi_frame_info_t frame, esp_err_t result) {
    taskENTER_CRITICAL(&lock);
    ++target->packets;
    target->bytes += len;
    if (result != ESP_OK) { ++target->errors; target->last_error = result; }
    if (frame.arp) ++target->arp;
    if (frame.dhcp) { ++target->dhcp; target->type = frame.dhcp_type; target->xid = frame.xid; }
    taskEXIT_CRITICAL(&lock);
    // DHCP headers only: never print credentials or packet payloads.
    if (frame.dhcp) ESP_LOGI(TAG, "DHCP %s %s xid=%08" PRIx32 " bytes=%u result=%s",
                            target == &tx ? "TX" : "RX", dhcp_type(frame.dhcp_type), frame.xid,
                            (unsigned)len, esp_err_to_name(result));
}

static esp_err_t transmit(void *handle, void *buffer, size_t len) {
    ard_wifi_frame_info_t frame = ard_wifi_classify_frame(buffer, len);
    // Saved callbacks are installed before Wi-Fi starts and never changed live.
    esp_err_t result = original.transmit(handle, buffer, len);
    count(&tx, len, frame, result);
    return result;
}

static esp_err_t transmit_wrap(void *handle, void *buffer, size_t len, void *netstack_buffer) {
    ard_wifi_frame_info_t frame = ard_wifi_classify_frame(buffer, len);
    esp_err_t result = original.transmit_wrap(handle, buffer, len, netstack_buffer);
    count(&tx, len, frame, result);
    return result;
}

esp_err_t __wrap_esp_netif_set_driver_config(esp_netif_t *netif, const esp_netif_driver_ifconfig_t *config) {
    const char *key = netif ? esp_netif_get_ifkey(netif) : NULL;
    if (!config || !key || strcmp(key, "WIFI_STA_DEF") || config->transmit == transmit ||
        config->transmit_wrap == transmit_wrap)
        return __real_esp_netif_set_driver_config(netif, config);
    esp_netif_driver_ifconfig_t traced = *config;
    esp_netif_driver_ifconfig_t previous = original;
    esp_netif_t *previous_station = station;
    original = *config;
    station = netif;
    if (traced.transmit) traced.transmit = transmit;
    if (traced.transmit_wrap) traced.transmit_wrap = transmit_wrap;
    esp_err_t result = __real_esp_netif_set_driver_config(netif, &traced);
    if (result != ESP_OK) { original = previous; station = previous_station; }
    ESP_LOGI(TAG, "STA data-path trace installed=%d tx=%d tx_wrap=%d result=%s",
             result == ESP_OK, config->transmit != NULL, config->transmit_wrap != NULL,
             esp_err_to_name(result));
    return result;
}

esp_err_t __wrap_esp_netif_receive(esp_netif_t *netif, void *buffer, size_t len, void *eb) {
    bool is_station = netif && netif == station;
    ard_wifi_frame_info_t frame = {0};
    if (is_station) frame = ard_wifi_classify_frame(buffer, len);
    // The real receiver may release buffer immediately: use the copied metadata only.
    esp_err_t result = __real_esp_netif_receive(netif, buffer, len, eb);
    if (is_station) count(&rx, len, frame, result);
    return result;
}

void wifi_debug_init(void) {
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "DHCP trace enabled; app=%s version=%s built=%s %s IDF=%s",
             app->project_name, app->version, app->date, app->time, app->idf_ver);
}

void wifi_debug_dump(const char *context) {
    traffic_t sent, received;
    taskENTER_CRITICAL(&lock);
    sent = tx; received = rx;
    taskEXIT_CRITICAL(&lock);
    esp_netif_t *netif = station;
    if (!netif) { ESP_LOGW(TAG, "%s: STA data-path not installed", context); return; }
    esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_INIT;
    esp_netif_ip_info_t ip = {0};
    uint8_t mac[6] = {0};
    esp_err_t dhcp_error = esp_netif_dhcpc_get_status(netif, &status);
    esp_err_t ip_error = esp_netif_get_ip_info(netif, &ip);
    esp_err_t mac_error = esp_netif_get_mac(netif, mac);
    ESP_LOGI(TAG, "%s: up=%d dhcpc=%d(%s) ip=" IPSTR " gw=" IPSTR " mask=" IPSTR " ip_result=%s",
             context, esp_netif_is_netif_up(netif), (int)status, esp_err_to_name(dhcp_error),
             IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask), esp_err_to_name(ip_error));
    ESP_LOGI(TAG, "STA MAC %02x:%02x:%02x:%02x:%02x:%02x result=%s; TX packets=%" PRIu32
             " bytes=%" PRIu32 " errors=%" PRIu32 " last=%s DHCP=%" PRIu32 "(%s xid=%08" PRIx32 ") ARP=%" PRIu32,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], esp_err_to_name(mac_error),
             sent.packets, sent.bytes, sent.errors, esp_err_to_name(sent.last_error),
             sent.dhcp, dhcp_type(sent.type), sent.xid, sent.arp);
    ESP_LOGI(TAG, "RX packets=%" PRIu32 " bytes=%" PRIu32 " errors=%" PRIu32
             " last=%s DHCP=%" PRIu32 "(%s xid=%08" PRIx32 ") ARP=%" PRIu32,
             received.packets, received.bytes, received.errors, esp_err_to_name(received.last_error),
             received.dhcp, dhcp_type(received.type), received.xid, received.arp);
}

void wifi_debug_poll(const char *context) {
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&lock);
    bool due = now >= next_dump;
    if (due) next_dump = now + 5000000LL;
    taskEXIT_CRITICAL(&lock);
    if (due) wifi_debug_dump(context);
}
#endif
