#include "gps.h"
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include "board.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr uart_port_t kPort = UART_NUM_2;
constexpr uint32_t kFixHoldMs = 5000;

std::atomic<bool> enabled{false};
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
GpsSnapshot state = {};
uint32_t fixStamp = 0;

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// ddmm.mmmm / dddmm.mmmm + hemisphere -> signed degrees.
bool coordinate(const char *value, const char *hemisphere, double *out) {
    if (!*value || !*hemisphere) return false;
    const double raw = strtod(value, nullptr);
    const int degrees = (int)(raw / 100);
    double result = degrees + (raw - degrees * 100) / 60.0;
    if (*hemisphere == 'S' || *hemisphere == 'W') result = -result;
    *out = result;
    return true;
}

void sentence(char *line) {
    // Verify "*HH" checksum over the bytes between '$' and '*'.
    char *star = strrchr(line, '*');
    if (!star || hex(star[1]) < 0 || hex(star[2]) < 0) return;
    uint8_t sum = 0;
    for (char *p = line + 1; p < star; ++p) sum ^= (uint8_t)*p;
    if (sum != ((hex(star[1]) << 4) | hex(star[2]))) return;
    *star = 0;

    const char *field[16] = {};
    int count = 0;
    for (char *p = line; count < 16;) { // keeps empty fields, unlike strtok
        field[count++] = p;
        char *comma = strchr(p, ',');
        if (!comma) break;
        *comma = 0;
        p = comma + 1;
    }
    if (strlen(field[0]) < 6) return;
    const char *type = field[0] + 3; // skip "$GP"/"$GN"/...
    double lat, lng;
    bool fix = false;
    uint32_t sats = 0;
    bool hasSats = false;
    if (!strcmp(type, "RMC") && count > 6) {
        fix = field[2][0] == 'A' && coordinate(field[3], field[4], &lat) && coordinate(field[5], field[6], &lng);
    } else if (!strcmp(type, "GGA") && count > 7) {
        fix = atoi(field[6]) > 0 && coordinate(field[2], field[3], &lat) && coordinate(field[4], field[5], &lng);
        sats = (uint32_t)atoi(field[7]);
        hasSats = true;
    } else {
        return;
    }
    portENTER_CRITICAL(&lock);
    if (hasSats) state.satellites = sats;
    if (fix) {
        state.lat = lat;
        state.lng = lng;
        fixStamp = nowMs();
        state.fix = true;
    }
    portEXIT_CRITICAL(&lock);
}

void gpsTask(void *) {
    static char line[96];
    size_t length = 0;
    uint8_t buffer[128];
    for (;;) {
        const int read = uart_read_bytes(kPort, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
        if (read <= 0 || !enabled) { length = 0; continue; }
        portENTER_CRITICAL(&lock);
        state.chars += read;
        portEXIT_CRITICAL(&lock);
        for (int i = 0; i < read; ++i) {
            const char c = (char)buffer[i];
            if (c == '$') length = 0;
            if (c == '\r' || c == '\n') {
                if (length && line[0] == '$') { line[length] = 0; sentence(line); }
                length = 0;
            } else if (length < sizeof(line) - 1) {
                line[length++] = c;
            } else {
                length = 0; // overlong garbage
            }
        }
    }
}

} // namespace

esp_err_t gps_start() {
    uart_config_t config = {};
    config.baud_rate = GPS_BAUD;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    esp_err_t err = uart_driver_install(kPort, 1024, 256, 0, nullptr, 0);
    if (err == ESP_OK) err = uart_param_config(kPort, &config);
    if (err == ESP_OK) err = uart_set_pin(kPort, PIN_GPS_TX, PIN_GPS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;
    return xTaskCreatePinnedToCore(gpsTask, "gps", 3072, nullptr, 6, nullptr, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void gps_set_enabled(bool on) { enabled = on; }
bool gps_enabled() { return enabled; }

GpsSnapshot gps_snapshot() {
    portENTER_CRITICAL(&lock);
    GpsSnapshot snapshot = state;
    if (snapshot.fix && nowMs() - fixStamp > kFixHoldMs) snapshot.fix = state.fix = false;
    portEXIT_CRITICAL(&lock);
    snapshot.enabled = enabled;
    return snapshot;
}

void gps_send(const uint8_t *data, size_t size) { uart_write_bytes(kPort, data, size); }
