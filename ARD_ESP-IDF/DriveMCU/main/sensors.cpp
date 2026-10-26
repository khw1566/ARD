#include "sensors.h"
#include <math.h>
#include <atomic>
#include <string.h>
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
constexpr uint32_t kTofBudgetUs = 50000;
constexpr uint32_t kTofDataTimeoutMs = 200;
constexpr uint32_t kTofMaxAgeMs = 200;
constexpr uint32_t kBmpPeriodMs = 5000;
constexpr uint32_t kDisplayPeriodMs = 250;
constexpr uint32_t kTofRetryMs = 2000;  // a ToF that missed boot (XSHUT low, late power) is retried
constexpr uint8_t kTofAddress = 0x29;

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
uint32_t tofDataStamp = 0;
uint16_t tofSamples[3] = {};
uint8_t tofIndex = 0, tofCount = 0;
uint32_t tofErrors = 0;

void invalidateTof(const char *status) {
    portENTER_CRITICAL(&lock);
    tofValid = false;
    tofStatus = status;
    portEXIT_CRITICAL(&lock);
    tofIndex = tofCount = 0;
}

void failTof(const char *status, bool recover, uint16_t rawMm = 0, uint8_t rawStatus = 0) {
    invalidateTof(status);
    if (recover) tofReady = false;
    // Avoid flooding the logger when a target is absent or the bus is disconnected.
    if (++tofErrors == 1 || tofErrors % 20 == 0)
        log_async("ToF %s: raw=%umm device_status=%u errors=%lu recovery=%d",
                  status, (unsigned)rawMm, (unsigned)rawStatus, (unsigned long)tofErrors, recover);
}

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

// Status names tell the wiring fault apart: NO_DEVICE = no ACK at 0x29 (SDA/SCL, power,
// XSHUT held low), BAD_ID = something answers at 0x29 but is not a VL53L0X,
// INIT_FAIL = the sensor answered and then stopped during setup.
bool initTof(bool announce) {
#if ARD_TOF_ENABLED
    const char *before = tofStatus;
    tofReady = false;
    invalidateTof("INIT");
    const uint32_t errors = Wire.errorCount();
    tof.clearIoError();
    if (!Wire.probe(kTofAddress)) {
        tofStatus = "NO_DEVICE";
    } else {
        // A previous interrupted init can leave a private register page selected.
        // Return to page zero and soft-reset before running SPAD/ref calibration again.
        tof.writeReg(0xFF, 0x00);
        tof.writeReg(0x80, 0x00);
        tof.writeReg(VL53L0X::SOFT_RESET_GO2_SOFT_RESET_N, 0x00);
        delay(2);
        tof.writeReg(VL53L0X::SOFT_RESET_GO2_SOFT_RESET_N, 0x01);
        delay(10);
        const uint8_t id = tof.readReg(VL53L0X::IDENTIFICATION_MODEL_ID);
        tof.setTimeout(100); // bounded init calibration waits; runtime polling never waits
        (void)tof.timeoutOccurred();
        if (Wire.errorCount() != errors) {
            tofStatus = "I2C_ERROR";
        } else if (id != 0xEE) {
            tofStatus = "BAD_ID";
        } else if (!tof.init()) {
            tofStatus = "INIT_FAIL";
        } else if (!tof.setMeasurementTimingBudget(kTofBudgetUs)) {
            tofStatus = "CONFIG_FAIL";
        } else {
            tof.startContinuous(); // back-to-back, explicit 50 ms integration budget
            if (Wire.errorCount() == errors) {
                tofReady = true;
                tofStatus = "WARMUP"; // ready != a valid distance
                tofDataStamp = millis();
                log_async("ToF initialized: budget=%luus median=3 SDA=%d SCL=%d",
                          (unsigned long)kTofBudgetUs, PIN_I2C_SDA, PIN_I2C_SCL);
                return true;
            }
            tofStatus = "I2C_ERROR";
        }
        if (Wire.errorCount() != errors) tofStatus = "I2C_ERROR";
    }
    if (announce || strcmp(before, tofStatus) != 0)
        log_async("ToF not ready: %s (VL53L0X 0x29 on SDA GPIO%d / SCL GPIO%d, XSHUT must be high). Retrying every %lus",
                  (const char *)tofStatus, PIN_I2C_SDA, PIN_I2C_SCL, (unsigned long)(kTofRetryMs / 1000));
#else
    (void)announce;
#endif
    return false;
}

void initDevices() {
    scanBus();
    ahtReady = aht.begin();
    ESP_LOGI(TAG, "AHT20 %s", ahtReady ? "found" : "not found");
    bmpReady = bmp.begin(0x76) || bmp.begin(0x77);
    if (bmpReady) ESP_LOGI(TAG, "BMP280 found at 0x%02X", bmp.address());
    else ESP_LOGW(TAG, "BMP280 not found; check wiring/address");
#if ARD_TOF_ENABLED
    initTof(true);
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
    if (!tofReady) return;
    const uint32_t errors = Wire.errorCount();
    const uint8_t ready = tof.readReg(VL53L0X::RESULT_INTERRUPT_STATUS);
    if (Wire.errorCount() != errors) { failTof("I2C_ERROR", true); return; }
    if (!(ready & 0x07)) {
        // No busy-wait: yield the shared bus to other sensors and OLED each tick.
        if (now - tofDataStamp >= kTofDataTimeoutMs) failTof("TIMEOUT", true);
        return;
    }
    uint8_t frame[12] = {};
    tof.readMulti(VL53L0X::RESULT_RANGE_STATUS, frame, sizeof(frame));
    if (Wire.errorCount() != errors) { failTof("I2C_ERROR", true); return; }
    tof.writeReg(VL53L0X::SYSTEM_INTERRUPT_CLEAR, 0x01);
    if (Wire.errorCount() != errors) { failTof("I2C_ERROR", true); return; }
    tofDataStamp = millis();
    const uint16_t mm = ((uint16_t)frame[10] << 8) | frame[11];
    // ST raw device status 11 = ranging complete. This is NOT PAL RangeStatus 0.
    // The lightweight Pololu port does not implement ST's software sigma estimator.
    const uint8_t status = (frame[0] & 0x78) >> 3;
    if (status != 11) { failTof("RANGE_ERROR", false, mm, status); return; }
    if (mm == 0 || mm >= 2000) { failTof("OUT OF RANGE", false, mm, status); return; }
    tofSamples[tofIndex] = mm;
    tofIndex = (tofIndex + 1) % 3;
    if (tofCount < 3) ++tofCount;
    if (tofCount < 3) { tofStatus = "WARMUP"; return; }
    uint16_t a = tofSamples[0], b = tofSamples[1], c = tofSamples[2];
    if (a > b) { const uint16_t t = a; a = b; b = t; }
    if (b > c) { b = c; }
    if (a > b) { b = a; }
    // Three consecutive valid frames are required after any measurement error.
    portENTER_CRITICAL(&lock);
    tofDistance = b;
    tofStamp = tofDataStamp;
    tofValid = true;
    tofStatus = "OK";
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
        static uint32_t lastTofTry = 0;
        if (ARD_TOF_ENABLED && !tofReady && now - lastTofTry >= kTofRetryMs) {
            lastTofTry = now;
            initTof(false);
        }
        pollTof(millis()); // init/retry may have taken time
        pollEnvironment(millis());
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

bool sensors_tof(float *mm, uint32_t *ageMs, uint32_t *sampleStamp) {
    portENTER_CRITICAL(&lock);
    const bool valid = tofValid;
    const float distance = tofDistance;
    const uint32_t stamp = tofStamp;
    portEXIT_CRITICAL(&lock);
    const uint32_t age = millis() - stamp;
    if (ageMs) *ageMs = age;
    if (sampleStamp) *sampleStamp = stamp;
    if (mm) *mm = distance;
    return valid && age <= kTofMaxAgeMs;
}

float sensors_tof_last() {
    float distance;
    uint32_t age;
    return sensors_tof(&distance, &age) ? distance : 0;
}

const char *sensors_tof_status() {
    const char *status = tofStatus;
    if (!strcmp(status, "OK")) {
        float mm;
        uint32_t age;
        if (!sensors_tof(&mm, &age)) return "STALE";
    }
    return status;
}
void sensors_request_i2c_clock(uint32_t hz) { requestedClock = hz; }
bool sensors_oled_ready() { return oledReady; }
