#pragma once
// Minimal Arduino-Wire-style wrapper over the ESP-IDF i2c_master driver, so the
// ported AHT20/VL53L0X code keeps its register access shape. Not thread-safe:
// only the sensor task (the I2C bus owner) may use it.
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef bool boolean; // Arduino type used by the VL53L0X port

static inline uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }
static inline void delay(uint32_t ms) {
    const TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1);
}

class ArdWire {
public:
    esp_err_t begin(gpio_num_t sda, gpio_num_t scl, uint32_t hz);
    void setClock(uint32_t hz);      // takes effect on the next transfer
    uint32_t clock() const { return hz_; }
    void setTimeOut(uint16_t ms) { timeoutMs_ = ms; }
    bool probe(uint8_t address);

    void beginTransmission(uint8_t address);
    size_t write(uint8_t value);
    size_t write(const uint8_t *data, size_t size);
    uint8_t endTransmission(bool stop = true); // 0 ok, 1 overflow, 4 error, 5 timeout
    uint8_t requestFrom(uint8_t address, uint8_t count);
    int available() const { return rxLength_ - rxIndex_; }
    int read() { return rxIndex_ < rxLength_ ? rx_[rxIndex_++] : -1; }

    // Larger transfers (OLED pages) bypass the small Wire buffer.
    esp_err_t transmit(uint8_t address, const uint8_t *data, size_t size);
    esp_err_t readRegister(uint8_t address, uint8_t reg, uint8_t *data, size_t size);
    uint32_t errorCount() const { return errors_; } // sole bus owner only

private:
    i2c_master_dev_handle_t device(uint8_t address);
    int timeoutFor(size_t bytes) const;

    struct Slot { uint8_t address; i2c_master_dev_handle_t handle; };
    i2c_master_bus_handle_t bus_ = nullptr;
    Slot slots_[8] = {};
    uint32_t hz_ = 100000;
    uint16_t timeoutMs_ = 20;
    uint32_t errors_ = 0;
    uint8_t txAddress_ = 0;
    uint8_t tx_[64] = {};
    size_t txLength_ = 0;
    bool txOverflow_ = false;
    uint8_t rx_[32] = {};
    int rxLength_ = 0, rxIndex_ = 0;
};

extern ArdWire Wire;
