#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_http_server.h"
#include "cam_ov5647.h"
#include "board_config.h"
#include "ard_sender.h"
#include "ard_network.h"

static const char *TAG = "ESP32P4_CAM_ETH";
static esp_eth_handle_t s_eth_handle = NULL;
static httpd_handle_t s_http_server = NULL;
static httpd_handle_t s_stream_server = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace; boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

/* HTTP Handler for Root / (Dashboard with Embedded Video) */
static esp_err_t index_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP GET '/' requested");

    const char* resp_str =
        "<!DOCTYPE html>"
        "<html lang='ko'>"
        "<head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>ESP32-P4 OV5647 Streamer</title>"
        "<style>"
        "body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:#0f1117;color:#e1e4ea;text-align:center;padding:30px 15px;margin:0;}"
        ".container{max-width:840px;margin:0 auto;background:#181b26;border:1px solid #2a2f42;border-radius:16px;padding:24px;box-shadow:0 12px 40px rgba(0,0,0,0.6);}"
        "h1{color:#4ade80;font-size:24px;margin:0 0 8px 0;}"
        ".meta{color:#94a3b8;font-size:14px;margin-bottom:18px;}"
        ".badge{display:inline-block;padding:4px 12px;border-radius:20px;background:rgba(74,222,128,0.15);color:#4ade80;font-weight:700;font-size:12px;margin-bottom:12px;}"
        ".stream-box{border-radius:12px;overflow:hidden;background:#05070a;border:2px solid #333a52;position:relative;margin-bottom:18px;min-height:300px;display:flex;align-items:center;justify-content:center;}"
        ".stream-box img{width:100%;height:auto;display:block;}"
        ".footer-info{display:flex;justify-content:space-around;background:#12141d;border-radius:10px;padding:12px;font-size:13px;color:#cbd5e1;}"
        ".btn{display:inline-block;padding:8px 18px;background:#3b82f6;color:#fff;text-decoration:none;border-radius:6px;font-weight:600;font-size:14px;transition:0.2s ease;}"
        ".btn:hover{background:#2563eb;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='container'>"
        "<p><a href='/network'>Wi-Fi / PC 서버 설정</a></p>"
        "<span class='badge'>ETHERNET CAMERA TEST</span>"
        "<h1>ESP32-P4 OV5647 Camera Video Streamer</h1>"
        "<p class='meta'>RJ45 Ethernet (IP101) & 2-Lane MIPI-CSI (OV5647) 실시간 영상 피드</p>"
        "<div class='stream-box'>"
        "<img alt='Live Camera Feed' onerror=\"this.parentElement.innerHTML='<p style=\\'color:#f87171;\\'>카메라 영상을 불러올 수 없습니다. 케이블 연결을 확인하십시오.</p>';\">"
        "</div>"
        "<div class='footer-info'>"
        "<span><b>Sensor:</b> OV5647 (P5V04A)</span>"
        "<span><b>Resolution:</b> 800x640</span>"
        "<span><b>Encoding:</b> Hardware JPEG</span>"
        "<span><a href='#' target='_blank' class='btn'>전체화면 스트림</a></span>"
        "</div>"
        "</div>"
        "<script>const u='http://'+location.hostname+':81/stream';document.querySelector('img').src=u;document.querySelector('.btn').href=u;</script>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
}

/* HTTP Handler for /stream (MJPEG Streaming Loop) */
static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = ESP_OK;
    size_t jpg_buf_len = 0;
    uint8_t *jpg_buf = NULL;
    char part_buf[128];

    res = cam_ov5647_get_frame(&jpg_buf, &jpg_buf_len, 2000);
    if (res != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Camera has no frame. Check serial logs.");
    }
    cam_ov5647_return_frame(jpg_buf);

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    ESP_LOGI(TAG, "MJPEG Stream client connected: %s", req->uri);

    while (true) {
        res = cam_ov5647_get_frame(&jpg_buf, &jpg_buf_len, 500);
        if (res == ESP_OK && jpg_buf != NULL && jpg_buf_len > 0) {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, (unsigned int)jpg_buf_len);
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
            if (res == ESP_OK) {
                res = httpd_resp_send_chunk(req, part_buf, hlen);
            }
            if (res == ESP_OK) {
                res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_buf_len);
            }
            cam_ov5647_return_frame(jpg_buf);
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        if (res != ESP_OK) {
            ESP_LOGI(TAG, "Stream client disconnected");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return res;
}

/* HTTP Handler for Favicon */
static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* Start Web Server */
static httpd_handle_t start_webserver(void)
{
    if (s_http_server != NULL) {
        return s_http_server;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.max_open_sockets = 4;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t favicon_uri = {
        .uri       = "/favicon.ico",
        .method    = HTTP_GET,
        .handler   = favicon_handler,
        .user_ctx  = NULL
    };

    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        httpd_register_uri_handler(s_http_server, &index_uri);
        ESP_ERROR_CHECK(ard_network_register_http(s_http_server));
        config.server_port = 81;
        config.ctrl_port += 1;
        if (httpd_start(&s_stream_server, &config) == ESP_OK) {
            httpd_register_uri_handler(s_stream_server, &stream_uri);
        } else {
            ESP_LOGE(TAG, "Failed to start streaming server on port 81");
        }
        httpd_register_uri_handler(s_http_server, &favicon_uri);
        ESP_LOGI(TAG, "HTTP Web Server started successfully on port %d!", config.server_port);
        return s_http_server;
    }

    ESP_LOGE(TAG, "Failed to start HTTP Web Server");
    return NULL;
}

/* Ethernet Event Handler */
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Cable Connected (Link Up) - MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ard_network_eth(false, NULL);
        ESP_LOGW(TAG, "Ethernet Cable Disconnected (Link Down)!");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Interface Started");
        break;
    case ETHERNET_EVENT_STOP:
        ard_network_eth(false, NULL);
        ESP_LOGI(TAG, "Ethernet Interface Stopped");
        break;
    default:
        break;
    }
}

/* IP Event Handler */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "\n"
                  "======================================================\n"
                  "  [Ethernet Network Connected]\n"
                  "  ETH IP   : " IPSTR "\n"
                  "  ETH MASK : " IPSTR "\n"
                  "  ETH GW   : " IPSTR "\n"
                  "------------------------------------------------------\n"
                  "  >> Web Browser URL : http://" IPSTR "/\n"
                  "  >> Video Stream URL: http://" IPSTR ":81/stream\n"
                  "======================================================",
             IP2STR(&ip_info->ip), IP2STR(&ip_info->netmask), IP2STR(&ip_info->gw),
             IP2STR(&ip_info->ip), IP2STR(&ip_info->ip));

    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ip_info->ip));
    ard_network_eth(true, ip);
    /* Start Web Server after IP acquisition */
    start_webserver();
}

/* Initialize Ethernet hardware for ESP32-P4 */
static esp_err_t eth_init(void)
{
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    if (!mac) {
        ESP_LOGE(TAG, "Failed to create MAC instance");
        return ESP_FAIL;
    }

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ESP_ETH_PHY_ADDR_AUTO;
    phy_config.reset_gpio_num = BOARD_ETH_PHY_RESET_IO;
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    if (!phy) {
        ESP_LOGE(TAG, "Failed to create PHY instance");
        return ESP_FAIL;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &s_eth_handle));

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(s_eth_handle);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, glue));

    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " ESP32-P4 Ethernet & OV5647 Camera Video Streamer ");
    ESP_LOGI(TAG, "==================================================");

    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Netif & Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ard_sender_start();
    // Serve settings on SoftAP even without Ethernet/DHCP or a working camera.
    start_webserver();
    ard_network_start();
    // 3. Register Event Handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    // 4. Start Ethernet Driver
    ESP_ERROR_CHECK(eth_init());

    // 5. Initialize OV5647 MIPI-CSI Camera Pipeline
    esp_err_t cam_ret = cam_ov5647_init();
    if (cam_ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed (%s). Continuing in network-only mode.", esp_err_to_name(cam_ret));
    }
}
