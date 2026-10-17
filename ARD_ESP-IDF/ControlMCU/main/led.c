#include "led.h"
#include "ard_network.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inputs.h"
#include "led_strip.h"

#define FADE_MS    30
#define BRIGHTNESS 60  // of 255, same as strip.setBrightness(60)

static const char *TAG = "LED";
static led_strip_handle_t strip;

void led_mic(bool on) { gpio_set_level(PIN_BUTTON_LED, on); }

static void set_rgb(int r, int g, int b) {
    if (!strip) return;
    led_strip_set_pixel(strip, 0, r * BRIGHTNESS / 255, g * BRIGHTNESS / 255, b * BRIGHTNESS / 255);
    led_strip_refresh(strip);
}

static void led_task(void *arg) {
    int level = 0, step = 5;
    while (true) {
        level += step;
        if (level <= 0 || level >= 255) {
            step = -step;
            level = level < 0 ? 0 : level > 255 ? 255 : level;
        }
        control_state_t s;
        inputs_get(&s);
        if (!ard_network_connected()) set_rgb(level / 3, 0, 0);
        else if (s.mode == 'A') set_rgb(0, 0, level);
        else if (s.mode == 'B') set_rgb(0, level, 0);
        else set_rgb(level / 5, level / 15, 0);  // N: dim orange warning
        vTaskDelay(pdMS_TO_TICKS(FADE_MS));
    }
}

void led_start(void) {
    gpio_config_t out = {.pin_bit_mask = 1ULL << PIN_BUTTON_LED, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&out));
    led_mic(false);
    led_strip_config_t cfg = {
        .strip_gpio_num = PIN_RGB_LED,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt = {.clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10 * 1000 * 1000};
    if (led_strip_new_rmt_device(&cfg, &rmt, &strip) != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 init failed");
        strip = NULL;
    }
    set_rgb(255, 0, 0);  // boot: red
    configASSERT(xTaskCreate(led_task, "led", 3072, NULL, 2, NULL) == pdPASS);
}
