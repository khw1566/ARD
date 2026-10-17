#include "sensors.h"
#include <math.h>
#include <atomic>
#include "aht20.h"
#include "ard_wire.h"
#include "bmp280.h"
#include "board.h"
#include "display.h"
#include "ssd1306.h"
#include "telemetry.h"
#include "third_party/vl53l0x/VL53L0X.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "sensors";

namespace {

constexpr uint32_t kTickMs = 10;
constexpr uint32_t kTofPeriodMs = 50;
constexpr uint32_t kBmpPeriodMs = 5000;
constexpr uint32_t kDisplayPeriodMs = 250;

BoundedAht20 aht;
Bmp280 bmp;
VL53L0X tof;
Ssd1306 oled;
bool ahtReady, bmpReady;
std::atomic<bool> tofReady{false}, oledReady{false};
std::atomic<const char *> tofStatus{ARD_TOF_ENABLED ? "Ready" : "DISABLED"};
std::atomic<uint32_t> requestedClock{0};

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
EnvironmentSample environment;
float tofDistance = 0;
uint32_t tofStamp = 0;
bool tofValid = false;

void scanBus() {
    int found = 0;
    for (uint8_t address = 1; address < 127; ++address) {
        if (Wire.probe(address)) {
            ESP_LOGI(TAG, "I2C device found at 0x%02X", address);
            ++found;
        }
    }
    if (!found) ESP_LOGW(TAG, "No I2C devices found");
}

void initDevices() {
    scanBus();
    ahtReady = aht.begin();
    ESP_LOGI(TAG, "AHT20 %s", ahtReady ? "found" : "not found");
    bmpReady = bmp.begin(0x76) || bmp.begin(0x77);
    if (bmpReady) ESP_LOGI(TAG, "BMP280 found at 0x%02X", bmp.address());
    else ESP_LOGW(TAG, "BMP280 not found; check wiring/address");
#if ARD_TOF_ENABLED
    tof.setTimeout(60);
    if (tof.init()) {
        tof.startContinuous();
        tofReady = true;
        tofStatus = "OK";
        log_async("ToF Sensor Initialized");
    } else {
        tofStatus = "INIT_FAIL";
        log_async("ToF Sensor Init FAILED");
    }
#else
    ESP_LOGI(TAG, "ToF disabled: open-loop rail control");
#endif
    oledReady = oled.begin(0x3C);
    if (!oledReady) ESP_LOGW(TAG, "SSD1306 not found");
}

void pollEnvironment(uint32_t now) {
    static uint32_t lastBmp = 0;
    portENTER_CRITICAL(&lock);
    EnvironmentSample sample = environment;
    portEXIT_CRITICAL(&lock);
    float t, h;
    if (ahtReady && aht.poll(t, h)) { sample.temperature = t; sample.humidity = h; }
    sample.ahtValid = ahtReady && aht.healthy();
    if (bmpReady && (now - lastBmp >= kBmpPeriodMs || !lastBmp)) {
        lastBmp = now ? now : 1;
        float p;
        sample.bmpValid = bmp.readPressure(p);
        if (sample.bmpValid) sample.pressure = p;
    }
    portENTER_CRITICAL(&lock);
    environment = sample;
    portEXIT_CRITICAL(&lock);
}

void pollTof(uint32_t now) {
    static uint32_t last = 0;
    static float samples[5];
    static uint8_t index = 0, count = 0;
    if (!tofReady || now - last < kTofPeriodMs) return;
    last = now;
    const uint16_t mm = tof.readRangeContinuousMillimeters();
    if (tof.timeoutOccurred()) { tofStatus = "TIMEOUT"; return; }
    if (mm == 0 || mm > 2000) { tofStatus = "OUT OF RANGE"; return; }
    tofStatus = "OK";
    samples[index] = mm;
    index = (index + 1) % 5;
    if (count < 5) ++count;
    float sum = 0;
    for (uint8_t i = 0; i < count; ++i) sum += samples[i];
    portENTER_CRITICAL(&lock);
    tofDistance = sum / count;
    tofStamp = now;
    tofValid = true;
    portEXIT_CRITICAL(&lock);
}

void sensorTask(void *) {
    initDevices();
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    TickType_t wake = xTaskGetTickCount();
    uint32_t lastRender = 0;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kTickMs));
        esp_task_wdt_reset();
        const uint32_t now = millis();
        if (const uint32_t clock = requestedClock.exchange(0)) Wire.setClock(clock);
        pollEnvironment(now);
        pollTof(now);
        if (oledReady) {
            if (now - lastRender >= kDisplayPeriodMs) {
                lastRender = now;
                display_render(oled);
                oled.commit();
            }
            oled.flushNextPage(); // one 128-byte page per tick
        }
    }
}

} // namespace

esp_err_t sensors_start() {
    esp_err_t err = Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
    if (err != ESP_OK) return err;
    Wire.setTimeOut(20);
    return xTaskCreatePinnedToCore(sensorTask, "sensors", 6144, nullptr, 6, nullptr, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

EnvironmentSample sensors_environment() {
    portENTER_CRITICAL(&lock);
    EnvironmentSample sample = environment;
    portEXIT_CRITICAL(&lock);
    return sample;
}

bool sensors_tof_ready() { return tofReady; }

bool sensors_tof(float *mm, uint32_t *ageMs) {
    portENTER_CRITICAL(&lock);
    const bool valid = tofValid;
    const float distance = tofDistance;
    const uint32_t stamp = tofStamp;
    portEXIT_CRITICAL(&lock);
    if (!valid) return false;
    *mm = distance;
    *ageMs = millis() - stamp;
    return true;
}

float sensors_tof_last() {
    portENTER_CRITICAL(&lock);
    const float distance = tofDistance;
    portEXIT_CRITICAL(&lock);
    return distance;
}

const char *sensors_tof_status() { return tofStatus; }
void sensors_request_i2c_clock(uint32_t hz) { requestedClock = hz; }
bool sensors_oled_ready() { return oledReady; }
