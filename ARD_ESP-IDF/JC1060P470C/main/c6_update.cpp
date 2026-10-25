#include "c6_update.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_hosted.h"
#include "esp_hosted_api_types.h"
#include "esp_hosted_host_fw_ver.h"
#include "esp_hosted_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "ui.h"

static const char *TAG = "c6_fw";

#if ARD_C6_FW_EMBEDDED
extern const uint8_t c6Start[] asm("_binary_network_adapter_bin_start");
extern const uint8_t c6End[] asm("_binary_network_adapter_bin_end");

namespace {

constexpr size_t kChunk = 1500;   // same chunk size as the ESP-Hosted host_performs_slave_ota example
constexpr uint8_t kMaxAttempts = 2;

uint32_t versionValue(uint32_t major, uint32_t minor, uint32_t patch) {
    return (major << 16) | (minor << 8) | patch;
}

// Version string from the image's esp_app_desc_t (after the image and first segment headers).
bool imageVersion(uint32_t *value, char *text, size_t size) {
    const size_t descOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if ((size_t)(c6End - c6Start) < descOffset + sizeof(esp_app_desc_t) || c6Start[0] != ESP_IMAGE_HEADER_MAGIC)
        return false;
    esp_app_desc_t desc;
    memcpy(&desc, c6Start + descOffset, sizeof(desc));
    if (desc.magic_word != ESP_APP_DESC_MAGIC_WORD) return false;
    strlcpy(text, desc.version, size);
    unsigned major = 0, minor = 0, patch = 0;
    if (sscanf(desc.version, "%u.%u.%u", &major, &minor, &patch) != 3) return false;
    *value = versionValue(major, minor, patch);
    return true;
}

// Attempts per embedded image version, so a C6 that cannot take the update
// does not put the board into a restart loop.
uint8_t attempts(uint32_t image, bool increment) {
    nvs_handle_t nvs;
    if (nvs_open("ard-c6", NVS_READWRITE, &nvs) != ESP_OK) return kMaxAttempts;
    uint32_t stored = 0;
    uint8_t count = 0;
    if (nvs_get_u32(nvs, "image", &stored) != ESP_OK || stored != image) count = 0;
    else nvs_get_u8(nvs, "count", &count);
    if (increment) {
        ++count;
        nvs_set_u32(nvs, "image", image);
        nvs_set_u8(nvs, "count", count);
        nvs_commit(nvs);
    }
    nvs_close(nvs);
    return count;
}

esp_err_t transfer() {
    esp_err_t err = esp_hosted_slave_ota_begin();
    if (err != ESP_OK) return err;
    const size_t total = c6End - c6Start;
    uint8_t chunk[kChunk];
    for (size_t offset = 0; offset < total; offset += kChunk) {
        const size_t n = total - offset < kChunk ? total - offset : kChunk;
        memcpy(chunk, c6Start + offset, n); // the API takes a mutable buffer
        if ((err = esp_hosted_slave_ota_write(chunk, n)) != ESP_OK) {
            esp_hosted_slave_ota_end();
            return err;
        }
        if (offset % (kChunk * 100) == 0)
            ESP_LOGI(TAG, "C6 update %u%%", (unsigned)(offset * 100 / total));
    }
    return esp_hosted_slave_ota_end();
}

} // namespace
#endif

esp_err_t c6_firmware_check() {
    esp_err_t err = esp_hosted_connect_to_slave();
    if (err != ESP_OK) return err;
    esp_hosted_coprocessor_fwver_t running = {};
    if ((err = esp_hosted_get_coprocessor_fwversion(&running)) != ESP_OK) return err;
    const uint32_t current = versionValue(running.major1, running.minor1, running.patch1);
    ESP_LOGI(TAG, "C6 ESP-Hosted %" PRIu32 ".%" PRIu32 ".%" PRIu32 ", host %d.%d.%d", running.major1,
             running.minor1, running.patch1, ESP_HOSTED_VERSION_MAJOR_1, ESP_HOSTED_VERSION_MINOR_1,
             ESP_HOSTED_VERSION_PATCH_1);
#if ARD_C6_FW_EMBEDDED
    uint32_t image = 0;
    char imageText[32];
    if (!imageVersion(&image, imageText, sizeof(imageText))) {
        ESP_LOGE(TAG, "Embedded C6 image is not a valid ESP app image");
        return ESP_ERR_INVALID_STATE;
    }
    if (current >= image) return ESP_OK; // never downgrade
    if (attempts(image, false) >= kMaxAttempts) {
        ESP_LOGE(TAG, "C6 still older than %s after %u update attempts; flash it over its UART header",
                 imageText, (unsigned)kMaxAttempts);
        return ESP_ERR_INVALID_VERSION;
    }
    attempts(image, true);
    ESP_LOGW(TAG, "Updating C6 to ESP-Hosted %s over SDIO (%u bytes)", imageText, (unsigned)(c6End - c6Start));
    ui_notice("Updating Wi-Fi co-processor (ESP32-C6)...\nDo not power off. The board restarts when done.");
    err = transfer();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "C6 update failed: %s; the C6 keeps its current firmware", esp_err_to_name(err));
        ui_notice(nullptr);
        return err;
    }
    // Slaves before 2.6 switch to the new image without an activate request.
    if (running.major1 > 2 || (running.major1 == 2 && running.minor1 > 5)) {
        err = esp_hosted_slave_ota_activate();
        if (err != ESP_OK) ESP_LOGW(TAG, "C6 activate: %s", esp_err_to_name(err));
    }
    ESP_LOGW(TAG, "C6 update written; restarting to resync with the C6");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
#else
    if (running.major1 != ESP_HOSTED_VERSION_MAJOR_1 || running.minor1 != ESP_HOSTED_VERSION_MINOR_1)
        ESP_LOGW(TAG, "C6 firmware differs from the host; Wi-Fi data and BLE may fail (no C6 image embedded)");
    return ESP_OK;
#endif
}
