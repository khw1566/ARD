#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "cam_ov5647.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "driver/i2c_master.h"
#include "driver/isp.h"
#include "driver/jpeg_encode.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"
#include "esp_sccb_i2c.h"
#include "esp_cam_sensor.h"
#include "ov5647.h"

static const char *TAG = "CAM_OV5647";

#define CAM_WIDTH                   800
#define CAM_HEIGHT                  640

#define CAM_LDO_CHAN_ID             3
#define CAM_LDO_VOLTAGE_MV          2500

#define CAM_FORMAT_NAME             "MIPI_2lane_24Minput_RAW8_800x640_50fps"
#define JPEG_MAX_BUFFER_SIZE        (256 * 1024)

static esp_ldo_channel_handle_t     s_ldo_handle = NULL;
static i2c_master_bus_handle_t      s_i2c_bus = NULL;
static esp_cam_sensor_device_t     *s_cam_dev = NULL;
static esp_cam_ctlr_handle_t        s_cam_ctlr = NULL;
static isp_proc_handle_t            s_isp_proc = NULL;
static jpeg_encoder_handle_t        s_jpeg_encoder = NULL;

static void                        *s_rgb_frame_buf = NULL;
static QueueHandle_t s_free_frames;
static QueueHandle_t s_ready_frames;
static size_t                       s_rgb_buf_size = 0;

static uint8_t                     *s_jpeg_out_buf = NULL;
static uint32_t                     s_jpeg_out_len = 0;
static SemaphoreHandle_t            s_jpeg_mutex = NULL;
static bool                         s_cam_initialized = false;
static int64_t s_jpeg_time;
static uint32_t s_quality = 80;
#include "esp_timer.h"

esp_err_t cam_ov5647_set_quality(uint32_t quality) {
    if (quality < 1 || quality > 100) return ESP_ERR_INVALID_ARG;
    if (!s_jpeg_mutex || xSemaphoreTake(s_jpeg_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_INVALID_STATE;
    s_quality = quality;
    xSemaphoreGive(s_jpeg_mutex);
    return ESP_OK;
}

static bool IRAM_ATTR s_cam_get_new_buffer(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    BaseType_t wake = pdFALSE;
    trans->buffer = NULL;
    trans->buflen = s_rgb_buf_size;
    xQueueReceiveFromISR(s_free_frames, &trans->buffer, &wake);
    return wake == pdTRUE;
}

static bool IRAM_ATTR s_cam_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    if (xQueueSendFromISR(s_ready_frames, &trans->buffer, &high_task_wakeup) != pdTRUE) {
        xQueueSendFromISR(s_free_frames, &trans->buffer, &high_task_wakeup);
    }
    return high_task_wakeup == pdTRUE;
}

static void cam_capture_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Camera capture & JPEG encode task started");
    bool first_frame = true;
    bool first_jpeg = true;

    jpeg_encode_cfg_t enc_cfg = {
        .width = CAM_WIDTH,
        .height = CAM_HEIGHT,
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
        .image_quality = 80,
        .pixel_reverse = false,
    };

    while (1) {
        void *frame = NULL;
        if (xQueueReceive(s_ready_frames, &frame, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (first_frame) {
                ESP_LOGI(TAG, "First CSI frame received: %dx%d RGB565, %zu bytes", CAM_WIDTH, CAM_HEIGHT, s_rgb_buf_size);
                first_frame = false;
            }
            if (xSemaphoreTake(s_jpeg_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                xQueueSend(s_free_frames, &frame, portMAX_DELAY);
                continue;
            }
            enc_cfg.image_quality = s_quality;
            uint32_t out_size = 0;
            esp_err_t ret = jpeg_encoder_process(s_jpeg_encoder, &enc_cfg,
                                                 (const uint8_t *)frame, (uint32_t)s_rgb_buf_size,
                                                 s_jpeg_out_buf, JPEG_MAX_BUFFER_SIZE, &out_size);
            if (ret == ESP_OK && out_size > 0) {
                s_jpeg_out_len = out_size;
                s_jpeg_time = esp_timer_get_time();
                if (first_jpeg) {
                    ESP_LOGI(TAG, "First JPEG encoded: %" PRIu32 " bytes; camera video is ready", out_size);
                    first_jpeg = false;
                }
            } else {
                s_jpeg_out_len = 0;
                ESP_LOGW(TAG, "jpeg_encoder_process failed or 0 bytes: %s", esp_err_to_name(ret));
            }
            xSemaphoreGive(s_jpeg_mutex);
            xQueueSend(s_free_frames, &frame, portMAX_DELAY);
        } else {
            ESP_LOGW(TAG, "No CSI frame in 500ms: check camera power, clock, ribbon and CSI pinout");
        }
    }
}

esp_err_t cam_ov5647_init(void)
{
    if (s_cam_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "Waveshare P4 Module DEV KIT: CAMERA/CSI J4, SDA=%d SCL=%d, OV5647 SCCB=0x36",
             CAM_SCCB_SDA_IO, CAM_SCCB_SCL_IO);
    ESP_LOGI(TAG, "  Initializing OV5647 (P5V04A) MIPI-CSI Camera");
    ESP_LOGI(TAG, "==============================================");

    // 1. Power on MIPI D-PHY via Internal LDO (Channel 3, 2.5V)
    esp_ldo_channel_config_t ldo_config = {
        .chan_id = CAM_LDO_CHAN_ID,
        .voltage_mv = CAM_LDO_VOLTAGE_MV,
    };
    esp_err_t ret = esp_ldo_acquire_channel(&ldo_config, &s_ldo_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MIPI LDO failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Initialize I2C Master Bus for SCCB
    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = CAM_SCCB_SDA_IO,
        .scl_io_num = CAM_SCCB_SCL_IO,
        .i2c_port = I2C_NUM_0,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus_conf, &s_i2c_bus), TAG, "camera pipeline setup");
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_i2c_bus, OV5647_SCCB_ADDR, 1000), TAG,
                        "OV5647 address 0x36 did not ACK; verify board_config.h pins, power and clock");

    // 3. Create SCCB I2C Interface
    sccb_i2c_config_t sccb_cfg = {
        .scl_speed_hz = 100000,
        .device_address = OV5647_SCCB_ADDR,
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    };
    esp_sccb_io_handle_t sccb_handle = NULL;
    ESP_RETURN_ON_ERROR(sccb_new_i2c_io(s_i2c_bus, &sccb_cfg, &sccb_handle), TAG, "camera pipeline setup");
    uint8_t id_high = 0, id_low = 0;
    ESP_RETURN_ON_ERROR(esp_sccb_transmit_receive_reg_a16v8(sccb_handle, 0x300a, &id_high), TAG, "sensor ID high read");
    ESP_RETURN_ON_ERROR(esp_sccb_transmit_receive_reg_a16v8(sccb_handle, 0x300b, &id_low), TAG, "sensor ID low read");
    ESP_LOGI(TAG, "Camera sensor ID: 0x%02x%02x (OV5647 expects 0x5647)", id_high, id_low);
    ESP_RETURN_ON_FALSE(id_high == 0x56 && id_low == 0x47, ESP_ERR_NOT_SUPPORTED, TAG,
                        "Camera is not detected as OV5647; do not apply OV5647 register settings");

    // 4. Detect OV5647 Camera Sensor
    esp_cam_sensor_config_t sensor_cfg = {
        .sccb_handle = sccb_handle,
        .reset_pin = -1,
        .pwdn_pin = -1,
        .xclk_pin = -1,
        .sensor_port = ESP_CAM_SENSOR_MIPI_CSI,
    };
    s_cam_dev = ov5647_detect(&sensor_cfg);
    if (!s_cam_dev) {
        ESP_LOGE(TAG, "Failed to detect OV5647 camera sensor! Check ribbon cable connection.");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OV5647 sensor detected successfully!");

    // 5. Query and Apply Format (800x640 RAW8 50fps)
    esp_cam_sensor_format_array_t cam_fmt_array = {0};
    ESP_RETURN_ON_ERROR(esp_cam_sensor_query_format(s_cam_dev, &cam_fmt_array), TAG, "query format");
    const esp_cam_sensor_format_t *target_fmt = NULL;
    for (int i = 0; i < cam_fmt_array.count; i++) {
        ESP_LOGI(TAG, "Available Format [%d]: %s", i, cam_fmt_array.format_array[i].name);
        if (strcmp(cam_fmt_array.format_array[i].name, CAM_FORMAT_NAME) == 0) {
            target_fmt = &cam_fmt_array.format_array[i];
        }
    }
    ESP_RETURN_ON_FALSE(target_fmt, ESP_ERR_NOT_SUPPORTED, TAG, "Enable the 800x640 RAW8 format in menuconfig");
    if (target_fmt) {
        ESP_LOGI(TAG, "Selected Format: %s (%dx%d)", target_fmt->name, target_fmt->width, target_fmt->height);
        ESP_RETURN_ON_ERROR(esp_cam_sensor_set_format(s_cam_dev, target_fmt), TAG, "camera pipeline setup");
    }

    // 6. Allocate RGB Framebuffer in PSRAM
    size_t alignment = 64;
    s_rgb_buf_size = CAM_WIDTH * CAM_HEIGHT * 2;
    s_free_frames = xQueueCreate(3, sizeof(void *));
    s_ready_frames = xQueueCreate(3, sizeof(void *));
    ESP_RETURN_ON_FALSE(s_free_frames && s_ready_frames, ESP_ERR_NO_MEM, TAG, "frame queues");
    for (int i = 0; i < 3; ++i) {
        s_rgb_frame_buf = heap_caps_aligned_calloc(alignment, 1, s_rgb_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_RETURN_ON_FALSE(s_rgb_frame_buf, ESP_ERR_NO_MEM, TAG, "PSRAM framebuffer");
        xQueueSend(s_free_frames, &s_rgb_frame_buf, 0);
    }


    // 7. Initialize MIPI-CSI Controller
    esp_cam_ctlr_csi_config_t csi_cfg = {
        .ctlr_id = 0,
        .h_res = CAM_WIDTH,
        .v_res = CAM_HEIGHT,
        .lane_bit_rate_mbps = target_fmt->mipi_info.mipi_clk / 1000000,
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RGB565,
        .data_lane_num = target_fmt->mipi_info.lane_num,
        .byte_swap_en = false,
        .queue_items = 1,
    };
    ESP_RETURN_ON_ERROR(esp_cam_new_csi_ctlr(&csi_cfg, &s_cam_ctlr), TAG, "camera pipeline setup");

    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = s_cam_get_new_buffer,
        .on_trans_finished = s_cam_trans_finished,
    };
    ESP_RETURN_ON_ERROR(esp_cam_ctlr_register_event_callbacks(s_cam_ctlr, &cbs, NULL), TAG, "camera pipeline setup");
    ESP_RETURN_ON_ERROR(esp_cam_ctlr_enable(s_cam_ctlr), TAG, "camera pipeline setup");

    // 8. Initialize Hardware ISP (RAW8 -> RGB565)
    esp_isp_processor_cfg_t isp_cfg = {
        .clk_hz = 120000000,
        .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type = ISP_COLOR_RAW8,
        .output_data_color_type = ISP_COLOR_RGB565,
        .has_line_start_packet = target_fmt->mipi_info.line_sync_en,
        .has_line_end_packet = target_fmt->mipi_info.line_sync_en,
        .bayer_order = COLOR_RAW_ELEMENT_ORDER_BGGR,
        .h_res = CAM_WIDTH,
        .v_res = CAM_HEIGHT,
    };
    ESP_RETURN_ON_ERROR(esp_isp_new_processor(&isp_cfg, &s_isp_proc), TAG, "camera pipeline setup");
    ESP_RETURN_ON_ERROR(esp_isp_enable(s_isp_proc), TAG, "camera pipeline setup");
    esp_isp_demosaic_config_t demosaic_cfg = { .grad_ratio = { .integer = 2, .decimal = 5 } };
    ESP_RETURN_ON_ERROR(esp_isp_demosaic_configure(s_isp_proc, &demosaic_cfg), TAG, "camera pipeline setup");
    ESP_RETURN_ON_ERROR(esp_isp_demosaic_enable(s_isp_proc), TAG, "camera pipeline setup");

    // 9. Initialize Hardware JPEG Encoder
    jpeg_encode_engine_cfg_t eng_cfg = {
        .intr_priority = 0,
        .timeout_ms = 100,
    };
    ESP_RETURN_ON_ERROR(jpeg_new_encoder_engine(&eng_cfg, &s_jpeg_encoder), TAG, "camera pipeline setup");

    jpeg_encode_memory_alloc_cfg_t mem_cfg = { .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER };
    size_t allocated_size = 0;
    s_jpeg_out_buf = jpeg_alloc_encoder_mem(JPEG_MAX_BUFFER_SIZE, &mem_cfg, &allocated_size);
    if (!s_jpeg_out_buf) {
        ESP_LOGE(TAG, "Failed to allocate JPEG output buffer");
        return ESP_ERR_NO_MEM;
    }

    s_jpeg_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_jpeg_mutex, ESP_ERR_NO_MEM, TAG, "JPEG mutex");

    // 10. Start CSI Capture
    ESP_RETURN_ON_ERROR(esp_cam_ctlr_start(s_cam_ctlr), TAG, "camera pipeline setup");
    int stream_on = 1;
    ESP_RETURN_ON_ERROR(esp_cam_sensor_ioctl(s_cam_dev, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_on), TAG, "camera pipeline setup");

    // 11. Start background encoding task
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(cam_capture_task, "cam_cap", 4096, NULL, 5, NULL, 1) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "capture task");

    s_cam_initialized = true;
    ESP_LOGI(TAG, "OV5647 MIPI-CSI pipeline running!");
    return ESP_OK;
}

esp_err_t cam_ov5647_get_frame(uint8_t **out_buf, size_t *out_len, uint32_t timeout_ms)
{
    if (!out_buf || !out_len) return ESP_ERR_INVALID_ARG;
    *out_buf = NULL;
    *out_len = 0;
    if (!s_cam_initialized || !s_jpeg_out_buf || !s_jpeg_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_jpeg_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        if (s_jpeg_out_len > 0 && esp_timer_get_time() - s_jpeg_time < 1000000) {
            *out_buf = s_jpeg_out_buf;
            *out_len = s_jpeg_out_len;
            return ESP_OK;
        }
        xSemaphoreGive(s_jpeg_mutex);
    }
    return ESP_ERR_TIMEOUT;
}

void cam_ov5647_return_frame(uint8_t *buf)
{
    if (s_jpeg_mutex) {
        xSemaphoreGive(s_jpeg_mutex);
    }
}
