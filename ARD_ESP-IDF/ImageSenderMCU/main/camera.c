#include "camera.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "board_pins.h"

static const char *TAG = "CAMERA";
static char cur_resolution[8] = "SVGA";
static int cur_quality = 12;
static bool running;
static char sensor_name[16] = "none"; // cached: read by other tasks during re-init

static const struct { const char *name; framesize_t size; } SIZES[] = {
    {"QSXGA", FRAMESIZE_QSXGA}, {"UXGA", FRAMESIZE_UXGA}, {"SXGA", FRAMESIZE_SXGA},
    {"XGA", FRAMESIZE_XGA}, {"SVGA", FRAMESIZE_SVGA}, {"VGA", FRAMESIZE_VGA},
    {"CIF", FRAMESIZE_CIF}, {"QVGA", FRAMESIZE_QVGA},
};

static bool lookup(const char *name, framesize_t *out) {
    for (size_t i = 0; i < sizeof(SIZES) / sizeof(SIZES[0]); ++i)
        if (!strcmp(SIZES[i].name, name)) { *out = SIZES[i].size; return true; }
    return false;
}

static int clamp_quality(int q) { return q < 0 ? 0 : q > 63 ? 63 : q; }

const char *camera_resolution(void) { return cur_resolution; }
int camera_quality(void) { return cur_quality; }

const char *camera_sensor_name(void) { return sensor_name; }

esp_err_t camera_start(const char *res, int q) {
    framesize_t size;
    if (!lookup(res, &size)) {
        ESP_LOGW(TAG, "Unknown resolution %s; using SVGA", res);
        res = "SVGA";
        size = FRAMESIZE_SVGA;
    }
    bool psram = esp_psram_get_size() > 0;
    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN, .pin_reset = CAM_PIN_RESET, .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD, .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7, .pin_d6 = CAM_PIN_D6, .pin_d5 = CAM_PIN_D5, .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3, .pin_d2 = CAM_PIN_D2, .pin_d1 = CAM_PIN_D1, .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC, .pin_href = CAM_PIN_HREF, .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = CAM_XCLK_HZ,
        .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = size,
        .jpeg_quality = clamp_quality(q),
        .fb_count = psram ? 2 : 1,
        .fb_location = psram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };
    ESP_LOGI(TAG, "Init %s Q%d, PSRAM=%s, fb_count=%d", res, config.jpeg_quality, psram ? "yes" : "no", config.fb_count);
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: %s (0x%x). Check ribbon cable and pins.", esp_err_to_name(err), err);
        running = false;
        return err;
    }
    running = true;
    strlcpy(cur_resolution, res, sizeof(cur_resolution));
    cur_quality = config.jpeg_quality;
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_exposure_ctrl(s, 1);
        s->set_gain_ctrl(s, 1);
        s->set_whitebal(s, 1);
        s->set_sharpness(s, 2);
        s->set_denoise(s, 1);
        s->set_quality(s, cur_quality);
        camera_sensor_info_t *info = esp_camera_sensor_get_info(&s->id);
        strlcpy(sensor_name, info ? info->name : "unknown", sizeof(sensor_name));
    }
    ESP_LOGI(TAG, "Camera ready: sensor %s, %s Q%d", camera_sensor_name(), cur_resolution, cur_quality);
    return ESP_OK;
}

esp_err_t camera_apply(const char *res, int q) {
    framesize_t size;
    if (!lookup(res, &size)) return ESP_ERR_INVALID_ARG;
    q = clamp_quality(q);
    if (!running || strcmp(res, cur_resolution)) {
        if (running) {
            ESP_LOGI(TAG, "Resolution %s -> %s: re-initialising", cur_resolution, res);
            esp_camera_deinit();
            running = false;
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        return camera_start(res, q);
    }
    if (q != cur_quality) {
        sensor_t *s = esp_camera_sensor_get();
        if (s && s->set_quality(s, q) == 0) {
            ESP_LOGI(TAG, "Quality %d -> %d", cur_quality, q);
            cur_quality = q;
        }
    }
    return ESP_OK;
}

camera_fb_t *camera_capture_fresh(void) {
    if (!running) return NULL;
    // With GRAB_LATEST and two buffers the first frame can predate the request.
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    for (int attempt = 0; attempt < 2; ++attempt) {
        fb = esp_camera_fb_get();
        if (fb && fb->format == PIXFORMAT_JPEG && fb->len > 4) return fb;
        if (fb) esp_camera_fb_return(fb);
        ESP_LOGW(TAG, "Frame grab failed (attempt %d)", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return NULL;
}
