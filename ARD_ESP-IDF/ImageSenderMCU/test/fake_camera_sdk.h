#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
#define LEDC_TIMER_0 0
#define LEDC_CHANNEL_0 0
typedef enum { FRAMESIZE_QSXGA, FRAMESIZE_UXGA, FRAMESIZE_SXGA,
    FRAMESIZE_XGA, FRAMESIZE_SVGA, FRAMESIZE_VGA, FRAMESIZE_CIF, FRAMESIZE_QVGA } framesize_t;
typedef struct { unsigned width, height; } resolution_info_t;
extern const resolution_info_t resolution[];
enum { PIXFORMAT_JPEG, CAMERA_FB_IN_PSRAM, CAMERA_FB_IN_DRAM,
       CAMERA_GRAB_LATEST, CAMERA_GRAB_WHEN_EMPTY };
typedef struct {
    int pin_pwdn, pin_reset, pin_xclk, pin_sccb_sda, pin_sccb_scl;
    int pin_d7, pin_d6, pin_d5, pin_d4, pin_d3, pin_d2, pin_d1, pin_d0;
    int pin_vsync, pin_href, pin_pclk, xclk_freq_hz, ledc_timer, ledc_channel;
    int pixel_format;
    framesize_t frame_size;
    int jpeg_quality;
    size_t fb_count;
    int fb_location, grab_mode;
    size_t jpeg_buffer_size;
} camera_config_t;
typedef struct { unsigned char *buf; size_t len; int format; } camera_fb_t;
typedef struct sensor_s {
    struct { unsigned PID; } id;
    int (*set_quality)(struct sensor_s *, int);
} sensor_t;
typedef struct { const char *name; } camera_sensor_info_t;
esp_err_t esp_camera_init(const camera_config_t *config);
esp_err_t esp_camera_deinit(void);
camera_fb_t *esp_camera_fb_get(void);
void esp_camera_fb_return(camera_fb_t *fb);
sensor_t *esp_camera_sensor_get(void);
camera_sensor_info_t *esp_camera_sensor_get_info(const void *id);
size_t esp_psram_get_size(void);
void vTaskDelay(unsigned ticks);
const char *esp_err_to_name(esp_err_t err);
static inline size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t n = strlen(src);
    if (size) { size_t copy = n < size - 1 ? n : size - 1; memcpy(dst, src, copy); dst[copy] = 0; }
    return n;
}
