#include "lcd.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "LCD";
static esp_lcd_panel_io_handle_t io;
static uint16_t *bands[2];
static int next_band;
static SemaphoreHandle_t free_bands;

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[15];
} init_cmd_t;

// TFT_eSPI ILI9341_2_DRIVER sequence (the one the PlatformIO build used).
static const init_cmd_t init_cmds[] = {
    {0xCF, 3, {0x00, 0xC1, 0x30}},
    {0xED, 4, {0x64, 0x03, 0x12, 0x81}},
    {0xE8, 3, {0x85, 0x00, 0x78}},
    {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}},
    {0xF7, 1, {0x20}},
    {0xEA, 2, {0x00, 0x00}},
    {0xC0, 1, {0x10}},             // PWCTR1
    {0xC1, 1, {0x00}},             // PWCTR2
    {0xC5, 2, {0x30, 0x30}},       // VMCTR1
    {0xC7, 1, {0xB7}},             // VMCTR2
    {0x3A, 1, {0x55}},             // 16-bit pixels
    {0x36, 1, {0x48}},             // MADCTL: MX | BGR = TFT_eSPI rotation 0
    {0xB1, 2, {0x00, 0x1A}},       // frame rate
    {0xB6, 3, {0x08, 0x82, 0x27}}, // display function control
    {0xF2, 1, {0x00}},             // 3-gamma off
    {0x26, 1, {0x01}},             // gamma curve
    {0xE0, 15, {0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9, 0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00}},
    {0xE1, 15, {0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56, 0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F}},
};

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(free_bands, &woken);
    return woken == pdTRUE;
}

void lcd_backlight(bool on) { gpio_set_level(LCD_PIN_BL, on); }

esp_err_t lcd_init(bool invert) {
    gpio_config_t bl = {.pin_bit_mask = 1ULL << LCD_PIN_BL, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&bl);
    lcd_backlight(false);

    const size_t band_bytes = LCD_W * LCD_BAND_H * sizeof(uint16_t);
    spi_bus_config_t bus = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = band_bytes,
    };
    esp_err_t err = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    free_bands = xSemaphoreCreateCounting(2, 2);
    for (int i = 0; i < 2; ++i) {
        bands[i] = heap_caps_malloc(band_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!bands[i] || !free_bands) return ESP_ERR_NO_MEM;
    }

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_SPI_HZ,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io);
    if (err != ESP_OK) return err;

    // CYD boards leave the ILI9341 reset line on the board reset; use the software reset.
    esp_lcd_panel_io_tx_param(io, 0x01, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    for (size_t i = 0; i < sizeof(init_cmds) / sizeof(init_cmds[0]); ++i)
        esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].len);
    esp_lcd_panel_io_tx_param(io, invert ? 0x21 : 0x20, NULL, 0);
    esp_lcd_panel_io_tx_param(io, 0x11, NULL, 0); // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    esp_lcd_panel_io_tx_param(io, 0x29, NULL, 0); // display on
    ESP_LOGI(TAG, "ILI9341 ready (%dx%d, %d MHz)", LCD_W, LCD_H, LCD_SPI_HZ / 1000000);
    return ESP_OK;
}

uint16_t *lcd_acquire_band(void) {
    xSemaphoreTake(free_bands, portMAX_DELAY);
    uint16_t *band = bands[next_band];
    next_band ^= 1;
    return band;
}

void lcd_flush(int x, int y, int w, int h, uint16_t *pixels) {
    const int x1 = x + w - 1, y1 = y + h - 1;
    uint8_t col[4] = {x >> 8, x & 0xFF, x1 >> 8, x1 & 0xFF};
    uint8_t row[4] = {y >> 8, y & 0xFF, y1 >> 8, y1 & 0xFF};
    esp_lcd_panel_io_tx_param(io, 0x2A, col, 4);
    esp_lcd_panel_io_tx_param(io, 0x2B, row, 4);
    esp_lcd_panel_io_tx_color(io, 0x2C, pixels, (size_t)w * h * sizeof(uint16_t));
}
