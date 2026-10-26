#include "display.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "buzzer.h"
#include "gps.h"
#include "motion.h"
#include "net/ard_network.h"
#include "sensors.h"
#include "esp_timer.h"
#include "esp_wifi.h"

namespace {

constexpr uint32_t kPageMs = 8000;
constexpr int kPages = 3;

bool localTime(struct tm *out) {
    time_t now = time(nullptr);
    localtime_r(&now, out);
    return out->tm_year + 1900 >= 2024; // SNTP has synced
}

void networkPage(Ssd1306 &oled) {
    char ip[16], server[65], ssid[33] = "-";
    ard_network_ip(ip, sizeof(ip));
    ard_network_server(server, sizeof(server));
    wifi_ap_record_t ap;
    if (ard_network_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        strlcpy(ssid, (const char *)ap.ssid, sizeof(ssid));
    oled.setCursor(0, 12); oled.printf("SSID: %.15s", ssid);
    oled.setCursor(0, 22); oled.printf("SRV : %.15s", server);
    oled.setCursor(0, 32); oled.printf("IP  : %s", ip);
    oled.setCursor(0, 42); oled.print("GPS : ");
    const GpsSnapshot gps = gps_snapshot();
    if (!gps.enabled) oled.print("DISABLED");
    else if (gps.fix) oled.printf("FIX (%lu SAT)", (unsigned long)gps.satellites);
    else oled.print("SEARCHING...");
}

void motionPage(Ssd1306 &oled) {
    MotionStatus s = {};
    const bool ok = motion_status(&s, 5);
    oled.setCursor(0, 12);
    if (ok) oled.printf("X:%.1f Y:%.1f deg", s.x * 1.8f, s.y * 1.8f);
    else oled.print("X/Y: busy");
    oled.setCursor(0, 22); oled.printf("Z TGT: %d mm %s", s.targetRailDist, s.pid ? (s.railSettled ? "OK" : "PID") : "OFF");
    float distance;
    uint32_t age;
    oled.setCursor(0, 32);
    if (sensors_tof(&distance, &age)) oled.printf("ToF : %.1f mm", distance);
    else oled.print("ToF : N/A");
    oled.setCursor(0, 42); oled.printf("Sts : %s", sensors_tof_status());
}

void environmentPage(Ssd1306 &oled, bool timeValid, const struct tm &t) {
    oled.setCursor(0, 12);
    if (timeValid) oled.printf("DATE: %04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    else oled.print("DATE: SYNCING...");
    const EnvironmentSample e = sensors_environment();
    oled.setCursor(0, 22);
    if (e.ahtValid) oled.printf("TEMP: %.1f C", e.temperature); else oled.print("TEMP: N/A");
    oled.setCursor(0, 32);
    if (e.ahtValid) oled.printf("HUMI: %.1f %%", e.humidity); else oled.print("HUMI: N/A");
    oled.setCursor(0, 42);
    if (e.bmpValid) oled.printf("PRES: %.1f hPa", e.pressure); else oled.print("PRES: N/A");
}

} // namespace

void display_render(Ssd1306 &oled) {
    const int page = (int)((esp_timer_get_time() / 1000 / kPageMs) % kPages);
    struct tm t;
    const bool timeValid = localTime(&t);
    oled.clear();
    oled.setCursor(0, 0);
    if (timeValid) oled.printf("[%d/3] %02d:%02d:%02d", page + 1, t.tm_hour, t.tm_min, t.tm_sec);
    else oled.printf("ARD [%d/3]", page + 1);
    oled.setCursor(95, 0);
    if (ard_network_connected()) {
        char ip[16];
        ard_network_ip(ip, sizeof(ip));
        const char *dot = strrchr(ip, '.');
        oled.print(dot ? dot : ip);
    } else {
        oled.print("OFFLINE");
    }
    oled.hline(0, 9, 128);
    switch (page) {
    case 0: networkPage(oled); break;
    case 1: motionPage(oled); break;
    default: environmentPage(oled, timeValid, t); break;
    }
    oled.hline(0, 52, 128);
    oled.setCursor(0, 55);
    oled.printf("RSSI:%d dBm  V:%d", ard_network_rssi(), buzzer_volume());
}
