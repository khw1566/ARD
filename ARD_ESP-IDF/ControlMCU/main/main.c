// ARD ControlMCU (ESP32-S3 N16R8, ESP-IDF 5.5)
// Replaces ARD_software/ControlMCU (Arduino). Module index:
//   inputs.c     debounced 3-position mode switch, voice button, 4-way joystick
//   led.c        WS2812 mode colour, voice button LED
//   audio.c      UDP 5005 PCM receiver -> jitter buffer -> I2S MAX98357A, volume
//   pc_link.c    /api/control/update + /api/heartbeat to the PC server
//   cmd_server.c TCP 5002 commands from the PC (vol, stop, mute, mic LED, ...)
//   components/ard_net  Wi-Fi AP+STA, mDNS, /network settings page (shared)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ard_network.h"
#include "audio.h"
#include "cmd_server.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inputs.h"
#include "led.h"
#include "nvs_flash.h"
#include "pc_link.h"
#include "sdkconfig.h"

static const char *TAG = "MAIN";

static void on_input_change(const control_state_t *s) { pc_link_notify_state(); }

static void print_help(void) {
    ESP_LOGI(TAG, "Serial: w=network setup, u/d=volume up/down, v<0-21><Enter>=volume, "
                  "s|x|q=stop all audio (board + PC), m=mute, p=unmute, i=status, ?=help");
}

static void console_task(void *arg) {
    // UART0 = the CH340K USB-UART port, same as the Arduino build.
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));
    print_help();
    char reply[96], num[4];
    int num_len = -1; // >= 0 while collecting the digits after 'v'
    uint8_t ch;
    while (true) {
        if (uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY) != 1) continue;
        if (num_len >= 0) {
            if (ch >= '0' && ch <= '9' && num_len < 2) {
                num[num_len++] = ch;
                continue;
            }
            num[num_len] = 0;
            char line[12];
            snprintf(line, sizeof(line), "vol=%s", num);
            cmd_execute(line, reply, sizeof(reply));
            ESP_LOGI(TAG, "%s", reply);
            num_len = -1;
            if (ch == '\r' || ch == '\n') continue;
        }
        const char *cmd = NULL;
        switch (ch) {
        case 'w': ard_network_print_setup(); break;
        case 'v': case 'V': num_len = 0; break;
        case 'u': case 'U': cmd = "vol+"; break;
        case 'd': case 'D': cmd = "vol-"; break;
        case 'm': case 'M': cmd = "mute"; break;
        case 'p': case 'P': cmd = "play"; break;
        case 'i': case 'I': cmd = "status"; break;
        case 's': case 'S': case 'x': case 'X': case 'q': case 'Q':
            cmd = "stop";
            pc_link_request_stop(); // the PC keeps streaming otherwise
            break;
        case '?': case 'h': print_help(); break;
        default: break;
        }
        if (cmd) {
            cmd_execute(cmd, reply, sizeof(reply));
            ESP_LOGI(TAG, "%s", reply);
        }
    }
}

static void start_web(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK || ard_network_register_http(server) != ESP_OK)
        ESP_LOGE(TAG, "Settings web server failed to start");
}

void app_main(void) {
    ESP_LOGI(TAG, "=== ARD ControlMCU (ESP-IDF) === reset reason %d", esp_reset_reason());
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    led_start();
    inputs_start(on_input_change);
    audio_start();

    // "wifi-config" is the Arduino ArdNetwork legacy namespace of this board.
    if (ard_network_start("control", "wifi-config", CONFIG_CONTROL_DEFAULT_SERVER) != ESP_OK)
        ESP_LOGE(TAG, "Network setup failed; settings AP may be unavailable");
    start_web();
    cmd_server_start();
    pc_link_start();
    configASSERT(xTaskCreate(console_task, "console", 3072, NULL, 2, NULL) == pdPASS);

#if CONFIG_CONTROL_BOOT_MELODY
    audio_boot_melody();
#endif
    ESP_LOGI(TAG, "Setup done");
}
