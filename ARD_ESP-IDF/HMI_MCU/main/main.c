// ARD HMI MCU (ESP32-2432S028) - ESP-IDF port of ARD_software/HMI_MCU.
#include <stdlib.h>
#include <time.h>
#include "ard_network.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hmi_net.h"
#include "hmi_ui.h"
#include "lcd.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "touch.h"

static const char *TAG = "HMI";

// Boot logo = PC program icon, pre-converted by tools/gen_logo.py:
// u16 width, u16 height (LE), then big-endian RGB565 pixels.
extern const uint8_t logo_start[] asm("_binary_logo_rgb565_start");

static uint16_t *acquire_band(void *user) { return lcd_acquire_band(); }
static void flush_band(void *user, int x, int y, int w, int h, uint16_t *px) { lcd_flush(x, y, w, h, px); }

static const gfx_target_t lcd_target = {
    .width = LCD_W,
    .height = LCD_H,
    .band_h = LCD_BAND_H,
    .acquire = acquire_band,
    .flush = flush_band,
};

// Touch is sampled every 15 ms. A press needs two consecutive hits and a release
// three consecutive misses, so one tap triggers exactly one action. The Arduino
// build repeated the action every 250 ms while a finger rested on a button.
static void ui_task(void *arg) {
    int hits = 0, misses = 0;
    bool down = false;
    while (true) {
        int x, y;
        if (touch_read(&x, &y)) {
            misses = 0;
            if (!down && ++hits >= 2) {
                down = true;
                hmi_ui_touch(x, y);
            }
        } else {
            hits = 0;
            if (down && ++misses >= 3) down = false;
        }
        hmi_ui_tick();
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

static void start_web(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK || ard_network_register_http(server) != ESP_OK)
        ESP_LOGE(TAG, "Settings web server failed to start");
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(lcd_init(CONFIG_HMI_LCD_INVERT));
    hmi_ui_init(&lcd_target);
    // Blitted straight from flash; gfx_image() copies rows with memcpy, so alignment is not an issue.
    const gfx_image_t logo = {
        .w = logo_start[0] | logo_start[1] << 8,
        .h = logo_start[2] | logo_start[3] << 8,
        .px = (uint16_t *)(logo_start + 4),
    };
    hmi_ui_splash(&logo, "Initializing...");
    lcd_backlight(true);

    if (touch_init() != ESP_OK) ESP_LOGE(TAG, "Touch controller init failed");

    hmi_ui_splash(&logo, "Starting network...");
    if (ard_network_start("hmi", "hmi-cfg", CONFIG_HMI_DEFAULT_SERVER) != ESP_OK)
        ESP_LOGE(TAG, "Network setup failed; settings AP may be unavailable");
    start_web();

    setenv("TZ", "KST-9", 1);
    tzset();
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);

    ESP_ERROR_CHECK(hmi_net_start());
    vTaskDelay(pdMS_TO_TICKS(800));

    hmi_ui_start();
    xTaskCreatePinnedToCore(ui_task, "hmi_ui", 6144, NULL, 5, NULL, 1);
}
