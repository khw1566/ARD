#ifndef WIFI_DEBUG_TEST_SDK_H
#define WIFI_DEBUG_TEST_SDK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONFIG_ARD_WIFI_DIAGNOSTICS 1
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGI(tag, ...) fake_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) fake_log(tag, __VA_ARGS__)
void fake_log(const char *, const char *, ...);
const char *esp_err_to_name(esp_err_t);
int64_t esp_timer_get_time(void);

typedef struct {
    void *handle;
    esp_err_t (*transmit)(void *, void *, size_t);
    esp_err_t (*transmit_wrap)(void *, void *, size_t, void *);
    void (*driver_free_rx_buffer)(void *, void *);
    esp_err_t (*driver_set_mac_filter)(void *, const uint8_t *, size_t, bool);
} esp_netif_driver_ifconfig_t;
typedef struct {
    const char *key;
    esp_netif_driver_ifconfig_t driver;
} esp_netif_t;
typedef enum { ESP_NETIF_DHCP_INIT, ESP_NETIF_DHCP_STARTED } esp_netif_dhcp_status_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip, gw, netmask; } esp_netif_ip_info_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) ((p)->addr & 255), (((p)->addr >> 8) & 255), \
                 (((p)->addr >> 16) & 255), (((p)->addr >> 24) & 255)
const char *esp_netif_get_ifkey(esp_netif_t *);
esp_err_t esp_netif_dhcpc_get_status(esp_netif_t *, esp_netif_dhcp_status_t *);
esp_err_t esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);
esp_err_t esp_netif_get_mac(esp_netif_t *, uint8_t *);
bool esp_netif_is_netif_up(esp_netif_t *);

typedef struct {
    const char *project_name, *version, *date, *time, *idf_ver;
} esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);
#endif
