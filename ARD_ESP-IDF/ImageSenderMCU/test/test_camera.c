#include <assert.h>
#include <stdio.h>
#include "fake_camera_sdk.h"
// Exercise the production capture wrapper; only the SDK/hardware is replaced.
#include "../main/camera.c"

const resolution_info_t resolution[] = {
    {2592,1944}, {1600,1200}, {1280,1024}, {1024,768},
    {800,600}, {640,480}, {352,288}, {320,240}
};
static size_t psram_bytes = 8 * 1024 * 1024;
static camera_config_t received_config;
static esp_err_t init_result;
static unsigned inits, deinits, quality_calls, gets, returns, delays;
static camera_fb_t stale = {.len=100, .format=PIXFORMAT_JPEG};
static camera_fb_t fresh = {.len=200, .format=PIXFORMAT_JPEG};
static camera_fb_t *queue[5], *returned[5];
static int set_quality(sensor_t *s, int q) { ++quality_calls; return 0; }
static sensor_t sensor = {.id={0x3660}, .set_quality=set_quality};
static camera_sensor_info_t info = {.name="OV3660"};
esp_err_t esp_camera_init(const camera_config_t *c) { ++inits; received_config=*c; return init_result; }
esp_err_t esp_camera_deinit(void) { ++deinits; return ESP_OK; }
camera_fb_t *esp_camera_fb_get(void) { assert(gets < 5); return queue[gets++]; }
void esp_camera_fb_return(camera_fb_t *fb) { assert(returns < 5); returned[returns++]=fb; }
sensor_t *esp_camera_sensor_get(void) { return &sensor; }
camera_sensor_info_t *esp_camera_sensor_get_info(const void *id) { return &info; }
size_t esp_psram_get_size(void) { return psram_bytes; }
void vTaskDelay(unsigned ticks) { ++delays; }
const char *esp_err_to_name(esp_err_t err) { return "fixture"; }

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "dram")) psram_bytes=0;
    if (!strcmp(argv[1], "init-failure")) init_result=ESP_FAIL;
    esp_err_t err=camera_start("SVGA", 12);
    if (!strcmp(argv[1], "init-failure")) {
        assert(err == ESP_FAIL && !running);
        assert(camera_capture_fresh() == NULL && gets == 0);
    } else {
        assert(err == ESP_OK && inits == 1);
        assert(received_config.fb_count == 1 && received_config.grab_mode == CAMERA_GRAB_WHEN_EMPTY);
        assert(received_config.xclk_freq_hz == 16000000);
        assert(received_config.pixel_format == PIXFORMAT_JPEG && received_config.jpeg_quality == 12);
        assert(received_config.frame_size == FRAMESIZE_SVGA && quality_calls == 0);
        assert(!strcmp(camera_sensor_name(), "OV3660"));
        assert(received_config.fb_location == (psram_bytes ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM));
        assert(received_config.jpeg_buffer_size == (psram_bytes ? 256 * 1024 : 0));
        if (!strcmp(argv[1], "ownership")) {
            queue[0]=&stale; queue[1]=&fresh;
            assert(camera_capture_fresh() == &fresh && gets == 2);
            assert(returns == 1 && returned[0] == &stale); // New frame remains held for transfer.
        } else if (!strcmp(argv[1], "retry")) {
            queue[0]=&stale; queue[1]=NULL; queue[2]=&fresh;
            assert(camera_capture_fresh() == &fresh && gets == 3);
            assert(returns == 1 && delays == 1);
        } else if (!strcmp(argv[1], "timeout")) {
            assert(camera_capture_fresh() == NULL && gets == 3 && delays == 2);
        } else if (!strcmp(argv[1], "apply")) {
            assert(camera_apply("SVGA", 15) == ESP_OK && quality_calls == 1 && inits == 1);
            assert(camera_apply("VGA", 15) == ESP_OK && deinits == 1 && inits == 2);
            assert(received_config.fb_count == 1 && received_config.grab_mode == CAMERA_GRAB_WHEN_EMPTY);
        }
    }
    printf("PASS camera %s\n", argv[1]);
    return 0;
}
