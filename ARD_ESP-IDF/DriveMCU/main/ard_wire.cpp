#include "ard_wire.h"
#include <string.h>
#include "esp_log.h"

ArdWire Wire;

esp_err_t ArdWire::begin(gpio_num_t sda, gpio_num_t scl, uint32_t hz) {
    hz_ = hz;
    i2c_master_bus_config_t config = {};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = sda;
    config.scl_io_num = scl;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    // Missing sensors NACK on every probe; the sensor task reports state itself.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    return i2c_new_master_bus(&config, &bus_);
}

void ArdWire::setClock(uint32_t hz) {
    if (hz == hz_) return;
    hz_ = hz;
    // Device handles carry their SCL speed; re-create them lazily.
    for (Slot &slot : slots_) {
        if (slot.handle) i2c_master_bus_rm_device(slot.handle);
        slot = {};
    }
}

i2c_master_dev_handle_t ArdWire::device(uint8_t address) {
    if (!bus_) return nullptr;
    Slot *free = nullptr;
    for (Slot &slot : slots_) {
        if (slot.handle && slot.address == address) return slot.handle;
        if (!slot.handle && !free) free = &slot;
    }
    if (!free) return nullptr;
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = hz_;
    if (i2c_master_bus_add_device(bus_, &config, &free->handle) != ESP_OK) return nullptr;
    free->address = address;
    return free->handle;
}

int ArdWire::timeoutFor(size_t bytes) const {
    // 9 clocks per byte plus margin; small register reads keep the short timeout.
    const int transfer = (int)((bytes + 1) * 9 * 1000 / hz_) + 5;
    return transfer > timeoutMs_ ? transfer * 2 : timeoutMs_;
}

bool ArdWire::probe(uint8_t address) {
    return bus_ && i2c_master_probe(bus_, address, timeoutMs_) == ESP_OK;
}

void ArdWire::beginTransmission(uint8_t address) {
    txAddress_ = address;
    txLength_ = 0;
    txOverflow_ = false;
}

size_t ArdWire::write(uint8_t value) {
    if (txLength_ >= sizeof(tx_)) { txOverflow_ = true; return 0; }
    tx_[txLength_++] = value;
    return 1;
}

size_t ArdWire::write(const uint8_t *data, size_t size) {
    size_t written = 0;
    while (written < size && write(data[written])) ++written;
    return written;
}

static uint8_t wireStatus(esp_err_t err) {
    if (err == ESP_OK) return 0;
    return err == ESP_ERR_TIMEOUT ? 5 : 4;
}

uint8_t ArdWire::endTransmission(bool) {
    if (txOverflow_) return 1;
    return wireStatus(transmit(txAddress_, tx_, txLength_));
}

esp_err_t ArdWire::transmit(uint8_t address, const uint8_t *data, size_t size) {
    i2c_master_dev_handle_t dev = device(address);
    const esp_err_t err = dev ? i2c_master_transmit(dev, data, size, timeoutFor(size)) : ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) ++errors_;
    return err;
}

esp_err_t ArdWire::readRegister(uint8_t address, uint8_t reg, uint8_t *data, size_t size) {
    i2c_master_dev_handle_t dev = device(address);
    const esp_err_t err = dev ? i2c_master_transmit_receive(dev, &reg, 1, data, size, timeoutFor(size + 1))
                             : ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) { ++errors_; memset(data, 0, size); }
    return err;
}

uint8_t ArdWire::requestFrom(uint8_t address, uint8_t count) {
    rxIndex_ = rxLength_ = 0;
    if (count > sizeof(rx_)) count = sizeof(rx_);
    i2c_master_dev_handle_t dev = device(address);
    if (!dev || !count || i2c_master_receive(dev, rx_, count, timeoutFor(count)) != ESP_OK) return 0;
    rxLength_ = count;
    return count;
}
