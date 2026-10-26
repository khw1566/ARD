#include "visitor_voice.h"
#include "onboard_audio.h"
#include "task_policy.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#include "mbedtls/sha256.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace {
constexpr size_t kMaxClip = 512 * 1024, kInternalReserve = 64 * 1024;
const char *names[] = {"welcome", "touch", "drive", "rail", "camera", "voice", "settings", "stop",
                       "rearm", "offline", "online", "sorting_start", "sorting_done", "safety"};
static_assert(sizeof(names) / sizeof(names[0]) == static_cast<unsigned>(VisitorVoice::Count));
struct Request { VisitorVoice id; bool korean, preview; uint32_t epoch; };
struct Cache { int key = -1; uint8_t *pcm = nullptr; size_t bytes = 0; } cache[2];
std::atomic<QueueHandle_t> requests{nullptr};
SemaphoreHandle_t storageLock;
std::atomic<uint32_t> epoch{0}, completed{0}, failed{0}, skipped{0}, cachedBytes{0};
std::atomic<bool> mounted{false};
sdmmc_card_t *card;
sd_pwr_ctrl_handle_t power;
uint32_t usedSlot;
int64_t lastMountAttempt = -30000000;
uint16_t u16(const uint8_t *p) { return p[0] | (p[1] << 8); }
uint32_t u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

esp_err_t mountCard() {
    if (esp_timer_get_time() - lastMountAttempt < 30000000) return ESP_ERR_TIMEOUT;
    lastMountAttempt = esp_timer_get_time();
    sd_pwr_ctrl_ldo_config_t ldo{};
    ldo.ldo_chan_id = 4; // JC1060 TF VO4; DSI uses LDO3, Hosted uses slot1.
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &power);
    if (err != ESP_OK) return err;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = 10000; // Conservative shared-host clock; match Hosted's 10 MHz policy.
    host.command_timeout_ms = 1000;
    host.pwr_ctrl_handle = power;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4; slot.clk = GPIO_NUM_43; slot.cmd = GPIO_NUM_44;
    slot.d0 = GPIO_NUM_39; slot.d1 = GPIO_NUM_40; slot.d2 = GPIO_NUM_41; slot.d3 = GPIO_NUM_42;
    esp_vfs_fat_mount_config_t config{};
    config.format_if_mount_failed = false; config.max_files = 3;
    err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &config, &card);
    if (err != ESP_OK) { sd_pwr_ctrl_del_on_chip_ldo(power); power = nullptr; }
    mounted.store(err == ESP_OK);
    return err;
}

bool waveData(FILE *file, size_t *bytes) {
    uint8_t header[12];
    if (fread(header, 1, 12, file) != 12 || memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4)) return false;
    if (fseek(file, 0, SEEK_END)) return false;
    const long fileBytes = ftell(file);
    if (fileBytes < 12 || uint64_t(u32(header + 4)) + 8 != static_cast<uint64_t>(fileBytes) || fseek(file, 12, SEEK_SET)) return false;
    bool format = false;
    // Bounded parser: chunks may contain metadata or an extended PCM fmt.
    for (unsigned chunks = 0; chunks < 64; ++chunks) {
        uint8_t chunk[8];
        if (fread(chunk, 1, 8, file) != 8) return false;
        const uint32_t length = u32(chunk + 4);
        const long dataAt = ftell(file);
        if (dataAt < 0 || uint64_t(dataAt) + length + (length & 1) > static_cast<uint64_t>(fileBytes)) return false;
        if (!memcmp(chunk, "fmt ", 4)) {
            uint8_t fmt[16];
            if (length < 16 || fread(fmt, 1, 16, file) != 16) return false;
            format = u16(fmt) == 1 && u16(fmt + 2) == 1 && u32(fmt + 4) == 16000 &&
                     u32(fmt + 8) == 32000 && u16(fmt + 12) == 2 && u16(fmt + 14) == 16;
        } else if (!memcmp(chunk, "data", 4)) {
            if (!format || !length || length > kMaxClip || (length & 1)) return false;
            *bytes = length;
            return true;
        }
        if (fseek(file, dataAt + length + (length & 1), SEEK_SET)) return false;
    }
    return false;
}

void play(const Request &request) {
    if (request.epoch != epoch.load()) return;
    const int key = static_cast<int>(request.id) * 2 + request.korean;
    Cache *hit = nullptr;
    for (auto &slot : cache) if (slot.key == key) hit = &slot;
    FILE *file = nullptr;
    size_t bytes = hit ? hit->bytes : 0;
    if (!hit) {
        char path[96];
        snprintf(path, sizeof(path), "/sdcard/ard/voice/%s/%s.wav", request.korean ? "ko" : "en", names[static_cast<int>(request.id)]);
        file = fopen(path, "rb");
        if (!file || !waveData(file, &bytes)) {
            if (file) fclose(file);
            ++failed; ESP_LOGW("visitor_voice", "Missing/invalid PCM16 mono 16kHz asset: %s", path); return;
        }
        // Two bounded, worker-owned PSRAM slots. Never fall back to internal RAM.
        Cache &slot = cache[usedSlot++ % 2];
        heap_caps_free(slot.pcm); slot = Cache{};
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= kInternalReserve)
            slot.pcm = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        const long start = ftell(file);
        if (slot.pcm) {
            size_t loaded = 0;
            while (loaded < bytes && request.epoch == epoch.load()) {
                size_t n = bytes - loaded > 4096 ? 4096 : bytes - loaded;
                const size_t read = fread(slot.pcm + loaded, 1, n, file);
                if (!read) break;
                loaded += read;
                vTaskDelay(1); // Large SD reads must not monopolize HP0.
            }
            if (loaded == bytes) { slot.key = key; slot.bytes = bytes; hit = &slot; }
            else { heap_caps_free(slot.pcm); slot = Cache{}; fseek(file, start, SEEK_SET); }
        }
        cachedBytes.store(cache[0].bytes + cache[1].bytes);
    }
    uint32_t token = 0;
    if (request.epoch != epoch.load() || !onboard_audio_guide_begin(request.id == VisitorVoice::Stop, request.preview, &token)) {
        if (file) fclose(file);
        ++skipped; return;
    }
    const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(bytes) * 1000000 / 32000 + 3000000;
    if (request.epoch != epoch.load()) {
        onboard_audio_guide_end(token);
        if (file) fclose(file);
        ++skipped;
        return;
    }
    int16_t chunk[512];
    size_t offset = 0;
    while (offset < bytes && request.epoch == epoch.load() && esp_timer_get_time() < deadline) {
        size_t length = bytes - offset > sizeof(chunk) ? sizeof(chunk) : bytes - offset;
        const int16_t *pcm;
        if (hit) pcm = reinterpret_cast<const int16_t *>(hit->pcm + offset);
        else {
            if (fread(chunk, 1, length, file) != length) break;
            pcm = chunk;
        }
        if (!onboard_audio_guide_push(token, pcm, length / 2)) break;
        offset += length;
    }
    onboard_audio_guide_end(token);
    if (file) fclose(file);
    if (offset == bytes) ++completed; else ++failed;
}

void worker(void *argument) {
    const QueueHandle_t queue = static_cast<QueueHandle_t>(argument);
    // Delay SD host init until ESP-Hosted has initialized the shared SDMMC host.
    // app_main starts this worker only after ard_network_start().
    xSemaphoreTake(storageLock, portMAX_DELAY);
    esp_err_t mountError = mountCard();
    xSemaphoreGive(storageLock);
    if (mountError != ESP_OK) ESP_LOGW("visitor_voice", "SD unavailable: %s; no formatting or flash fallback", esp_err_to_name(mountError));
    for (;;) {
        Request request;
        if (xQueueReceive(queue, &request, portMAX_DELAY) != pdTRUE) continue;
        xSemaphoreTake(storageLock, portMAX_DELAY);
        if (!mounted.load()) {
            // Explicit request retries a missing/reinserted card without restarting.
            mountError = mountCard();
            if (mountError != ESP_OK) { ++failed; xSemaphoreGive(storageLock); continue; }
        }
        play(request);
        xSemaphoreGive(storageLock);
    }
}
} // namespace

esp_err_t visitor_voice_start() {
    if (requests.load()) return ESP_ERR_INVALID_STATE;
    if (!storageLock) storageLock = xSemaphoreCreateMutex();
    if (!storageLock) return ESP_ERR_NO_MEM;
    const QueueHandle_t queue = xQueueCreate(4, sizeof(Request));
    if (!queue) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(worker, "visitor_voice", 6144, queue, 2, nullptr, ARD_CORE_SERVICE) != pdPASS) {
        vQueueDelete(queue); return ESP_ERR_NO_MEM;
    }
    requests.store(queue); // Publish only after startup succeeds; UI already runs on HP1.
    return ESP_OK;
}
void visitor_voice_cancel() {
    ++epoch;
    const QueueHandle_t queue = requests.load();
    if (queue) xQueueReset(queue);
    if (onboard_audio_guide_active()) onboard_audio_stop();
}
void visitor_voice_request(VisitorVoice id, bool korean, bool preview) {
    const QueueHandle_t queue = requests.load();
    if (!queue || static_cast<unsigned>(id) >= static_cast<unsigned>(VisitorVoice::Count)) return;
    const auto audio = onboard_audio_status();
    if (!audio.ready || audio.muted || audio.volume == 0 ||
        (!preview && id != VisitorVoice::Stop && !audio.guidance)) return;
    if (id == VisitorVoice::Stop) visitor_voice_cancel();
    Request request{id, korean, preview, epoch.load()};
    const BaseType_t sent = id == VisitorVoice::Stop ? xQueueSendToFront(queue, &request, 0) : xQueueSend(queue, &request, 0);
    if (sent != pdTRUE) ++skipped;
}
void visitor_voice_print_status() {
    ESP_LOGI("visitor_voice", "SD=%d PSRAM_cache=%lu/1048576 bytes queued_to_audio=%lu failed=%lu skipped=%lu",
             mounted.load(), static_cast<unsigned long>(cachedBytes.load()), static_cast<unsigned long>(completed.load()),
             static_cast<unsigned long>(failed.load()), static_cast<unsigned long>(skipped.load()));
}

bool visitor_voice_sd_ready() { return mounted.load(); }
const char *visitor_voice_name(VisitorVoice id) {
    return static_cast<unsigned>(id) < static_cast<unsigned>(VisitorVoice::Count) ? names[static_cast<unsigned>(id)] : nullptr;
}

namespace {
void voicePath(char *path, size_t size, VisitorVoice id, bool korean) {
    snprintf(path, size, "/sdcard/ard/voice/%s/%s.wav", korean ? "ko" : "en", visitor_voice_name(id));
}
esp_err_t hashFile(const char *path, size_t *length, char digest[65]) {
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    uint8_t block[1024], hash[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    int result = mbedtls_sha256_starts(&sha, 0);
    size_t total = 0;
    while (!result) {
        const size_t got = fread(block, 1, sizeof(block), file);
        if (!got) break;
        result = mbedtls_sha256_update(&sha, block, got);
        total += got;
        if (total > kMaxClip + 4096) { result = -1; break; }
        vTaskDelay(1);
    }
    if (ferror(file)) result = -1;
    fclose(file);
    if (!result) result = mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);
    if (result) return ESP_FAIL;
    for (unsigned i = 0; i < 32; ++i) snprintf(digest + i * 2, 3, "%02x", hash[i]);
    *length = total;
    return ESP_OK;
}
}

esp_err_t visitor_voice_hash(VisitorVoice id, bool korean, size_t *length, char digest[65]) {
    if (!visitor_voice_name(id) || !length || !digest) return ESP_ERR_INVALID_ARG;
    if (!storageLock || !mounted.load()) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(storageLock, pdMS_TO_TICKS(1500)) != pdTRUE) return ESP_ERR_TIMEOUT;
    char path[96]; voicePath(path, sizeof(path), id, korean);
    const esp_err_t result = hashFile(path, length, digest);
    xSemaphoreGive(storageLock);
    return result;
}

esp_err_t visitor_voice_store(VisitorVoice id, bool korean, const uint8_t *wav, size_t length,
                              const char *expectedSha256, char digest[65]) {
    if (!visitor_voice_name(id) || !wav || length < 44 || length > kMaxClip + 4096 || !expectedSha256 || strlen(expectedSha256) != 64)
        return ESP_ERR_INVALID_ARG;
    if (!storageLock || !mounted.load()) return ESP_ERR_INVALID_STATE;
    visitor_voice_cancel();
    if (xSemaphoreTake(storageLock, pdMS_TO_TICKS(2000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    char path[96], temporary[112], backup[112];
    voicePath(path, sizeof(path), id, korean);
    snprintf(temporary, sizeof(temporary), "%s.upload", path);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    esp_err_t result = ESP_OK;
    for (const char *dir : {"/sdcard/ard", "/sdcard/ard/voice", korean ? "/sdcard/ard/voice/ko" : "/sdcard/ard/voice/en"}) {
        if (mkdir(dir, 0775) && errno != EEXIST) result = ESP_FAIL;
    }
    FILE *file = result == ESP_OK ? fopen(temporary, "wb") : nullptr;
    if (!file) { ESP_LOGW("visitor_voice", "SD upload open failed errno=%d", errno); result = ESP_FAIL; }
    if (file) {
        for (size_t offset = 0; offset < length && result == ESP_OK;) {
            const size_t n = length - offset > 4096 ? 4096 : length - offset;
            if (fwrite(wav + offset, 1, n, file) != n) result = ESP_FAIL;
            offset += n;
            vTaskDelay(1);
        }
        if (fflush(file) || fsync(fileno(file))) result = ESP_FAIL;
        if (fclose(file)) result = ESP_FAIL;
    }
    if (result == ESP_OK) {
        file = fopen(temporary, "rb");
        size_t pcm = 0;
        if (!file || !waveData(file, &pcm)) result = ESP_ERR_INVALID_ARG;
        if (file) fclose(file);
    }
    size_t readBack = 0;
    if (result == ESP_OK) result = hashFile(temporary, &readBack, digest);
    if (result == ESP_OK && (readBack != length || strcmp(expectedSha256, digest))) result = ESP_ERR_INVALID_CRC;
    if (result == ESP_OK) {
        struct stat info;
        const bool hadOld = stat(path, &info) == 0;
        if (hadOld && unlink(backup) && errno != ENOENT) result = ESP_FAIL;
        if (result == ESP_OK && hadOld && rename(path, backup)) result = ESP_FAIL;
        if (result == ESP_OK && rename(temporary, path)) {
            if (hadOld) rename(backup, path);
            result = ESP_FAIL;
        }
        if (result == ESP_OK) {
            const int key = static_cast<int>(id) * 2 + korean;
            for (auto &slot : cache) if (slot.key == key) { heap_caps_free(slot.pcm); slot = Cache{}; }
            cachedBytes.store(cache[0].bytes + cache[1].bytes);
        }
    }
    if (result != ESP_OK) unlink(temporary);
    xSemaphoreGive(storageLock);
    return result;
}
