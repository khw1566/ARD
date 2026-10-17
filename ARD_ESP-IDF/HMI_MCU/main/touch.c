#include "touch.h"
#include <stdlib.h>
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "lcd.h"

// Calibration and pressure threshold from the Arduino CYD28_TouchscreenR driver.
#define CAL_XMIN 185
#define CAL_XMAX 3700
#define CAL_YMIN 280
#define CAL_YMAX 3850
#define Z_THRESHOLD 300

// XPT2046 control bytes. PD=01 keeps the ADC powered between samples;
// the final 0xD0 powers down and re-enables PENIRQ.
#define CMD_Z1 0xB1
#define CMD_Z2 0xC1
#define CMD_X  0x91
#define CMD_Y  0xD1
#define CMD_PD 0xD0

static spi_device_handle_t dev;

static int sample(uint8_t cmd) {
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 24,
        .tx_data = {cmd, 0, 0},
    };
    if (spi_device_polling_transmit(dev, &t) != ESP_OK) return 0;
    return ((t.rx_data[1] << 8) | t.rx_data[2]) >> 3;
}

// Average of the two closest of three samples (rejects one outlier).
static int best_two(int a, int b, int c) {
    int ab = abs(a - b), ac = abs(a - c), bc = abs(b - c);
    if (ab <= ac && ab <= bc) return (a + b) / 2;
    if (ac <= ab && ac <= bc) return (a + c) / 2;
    return (b + c) / 2;
}

esp_err_t touch_init(void) {
    gpio_config_t irq = {.pin_bit_mask = 1ULL << TOUCH_PIN_IRQ, .mode = GPIO_MODE_INPUT};
    gpio_config(&irq);
    spi_bus_config_t bus = {
        .sclk_io_num = TOUCH_PIN_SCLK,
        .mosi_io_num = TOUCH_PIN_MOSI,
        .miso_io_num = TOUCH_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4,
    };
    esp_err_t err = spi_bus_initialize(TOUCH_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) return err;
    spi_device_interface_config_t cfg = {
        .clock_speed_hz = TOUCH_SPI_HZ,
        .mode = 0,
        .spics_io_num = TOUCH_PIN_CS,
        .queue_size = 1,
    };
    err = spi_bus_add_device(TOUCH_HOST, &cfg, &dev);
    if (err == ESP_OK) sample(CMD_PD);
    return err;
}

bool touch_read(int *x, int *y) {
    // PENIRQ idles high; skip the SPI traffic entirely while nobody touches the panel.
    if (gpio_get_level(TOUCH_PIN_IRQ)) return false;
    if (spi_device_acquire_bus(dev, portMAX_DELAY) != ESP_OK) return false;
    int z = sample(CMD_Z1) + 4095 - sample(CMD_Z2);
    bool pressed = z >= Z_THRESHOLD;
    int xs[3] = {0}, ys[3] = {0};
    if (pressed) {
        sample(CMD_X); // first conversion after switching channel is noisy
        for (int i = 0; i < 3; ++i) {
            xs[i] = sample(CMD_X);
            ys[i] = sample(CMD_Y);
        }
    }
    sample(CMD_PD);
    spi_device_release_bus(dev);
    if (!pressed) return false;

    int raw_x = best_two(xs[0], xs[1], xs[2]);
    int raw_y = best_two(ys[0], ys[1], ys[2]);
    // Rotation 0 mapping of the Arduino driver: raw Y -> screen X (mirrored), raw X -> screen Y.
    int sx = LCD_W - (raw_y - CAL_YMIN) * LCD_W / (CAL_YMAX - CAL_YMIN);
    int sy = (raw_x - CAL_XMIN) * LCD_H / (CAL_XMAX - CAL_XMIN);
    *x = sx < 0 ? 0 : sx >= LCD_W ? LCD_W - 1 : sx;
    *y = sy < 0 ? 0 : sy >= LCD_H ? LCD_H - 1 : sy;
    return true;
}
