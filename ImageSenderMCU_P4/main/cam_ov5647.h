#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize OV5647 MIPI-CSI Camera, ISP, and JPEG Encoder
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t cam_ov5647_init(void);

/**
 * @brief Get latest captured JPEG frame
 *
 * @param[out] out_buf Pointer to JPEG buffer
 * @param[out] out_len Size of JPEG buffer in bytes
 * @param[in] timeout_ms Timeout in milliseconds to wait for new frame
 * @return esp_err_t ESP_OK on success
 */
esp_err_t cam_ov5647_get_frame(uint8_t **out_buf, size_t *out_len, uint32_t timeout_ms);

/**
 * @brief Release the JPEG frame buffer
 *
 * @param[in] buf Buffer pointer returned by cam_ov5647_get_frame
 */
void cam_ov5647_return_frame(uint8_t *buf);

#ifdef __cplusplus
}
#endif

esp_err_t cam_ov5647_set_quality(uint32_t quality);
