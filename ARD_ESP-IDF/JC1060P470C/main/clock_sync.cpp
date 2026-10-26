#include "clock_sync.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>
#include "ard_network.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"

namespace {
const char *TAG = "clock_sync";
void gotIp(void *, esp_event_base_t, int32_t, void *) {
    const esp_err_t err = esp_netif_sntp_start();
    if (err != ESP_OK) ESP_LOGW(TAG, "SNTP start: %s; PC time fallback remains available", esp_err_to_name(err));
}
}

esp_err_t clock_sync_start() {
    // POSIX time-zone offsets have the opposite sign: KST is UTC+9.
    setenv("TZ", "KST-9", 1);
    tzset();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.start = false;
    config.wait_for_sync = false;
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, gotIp, nullptr);
    if (err == ESP_OK && ard_network_connected()) gotIp(nullptr, IP_EVENT, IP_EVENT_STA_GOT_IP, nullptr);
    return err;
}

void clock_sync_from_pc(double unix_seconds) {
    // The time endpoint is optional for older PC software. Reject NaN, missing
    // values and dates outside 2024..2099 before converting to time_t.
    if (!std::isfinite(unix_seconds) || unix_seconds < 1704067200.0 || unix_seconds >= 4102444800.0) return;
    const time_t current = time(nullptr);
    if (std::fabs(unix_seconds - static_cast<double>(current)) <= 2.0) return;
    timeval next{};
    next.tv_sec = static_cast<time_t>(unix_seconds);
    next.tv_usec = static_cast<suseconds_t>((unix_seconds - static_cast<double>(next.tv_sec)) * 1000000.0);
    if (settimeofday(&next, nullptr) == 0) ESP_LOGI(TAG, "Clock synchronized from PC (KST)");
}
