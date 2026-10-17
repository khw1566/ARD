#include "pc_link.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include "ard_network.h"
#include "board.h"
#include "cJSON.h"
#include "driver/jpeg_decode.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "pc_link";

namespace {

constexpr uint32_t kPollMs = 3000;       // same cadence as HMI_MCU
constexpr uint32_t kHeartbeatMs = 10000;
constexpr size_t kStatusMax = 16384;
constexpr size_t kJpegMax = 512 * 1024;  // HMI_MCU capped at 40 KB; PSRAM allows full frames

struct Outgoing {
    enum Kind : uint8_t { COMMAND, APPROVAL } kind;
    PcBoard board;
    bool approve;
    char text[96];
};

const char *const kBoardKeys[PC_BOARDS] = {"drivemcu", "robot3axismcu", "imagesendermcu", "controlmcu"};
const char *const kCommandPaths[PC_BOARDS] = {"/api/drivemcu/command", "/api/robot3axis/command",
                                              "/api/imagesendermcu/command", "/api/controlmcu/command"};

SemaphoreHandle_t statusLock, imageLock;
PcStatus status = {};
QueueHandle_t outgoing;
std::atomic<bool> imageRequested{false};
TaskHandle_t worker;

jpeg_decoder_handle_t jpeg;
uint8_t *jpegBuffer;            // compressed stream
uint8_t *pixels;                // decoded RGB565
size_t pixelsSize;
PcImage image = {};

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

void makeUrl(char *out, size_t size, const char *path) {
    char host[65];
    ard_network_server(host, sizeof(host));
    snprintf(out, size, "http://%s:%d%s", host, ARD_PC_HTTP_PORT, path);
}

// GET/POST helper; reads at most `max` bytes of the body into `body` (NUL-terminated for text).
int http(const char *path, esp_http_client_method_t method, const char *json, int timeoutMs,
         uint8_t *body = nullptr, size_t max = 0, size_t *length = nullptr) {
    if (!ard_network_connected()) return -1;
    char target[128];
    makeUrl(target, sizeof(target), path);
    esp_http_client_config_t config = {};
    config.url = target;
    config.method = method;
    config.timeout_ms = timeoutMs;
    config.disable_auto_redirect = true;
    config.buffer_size = 2048;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -ESP_ERR_NO_MEM;
    const int sendLength = json ? (int)strlen(json) : 0;
    if (json) esp_http_client_set_header(client, "Content-Type", "application/json");
    int result;
    esp_err_t err = esp_http_client_open(client, sendLength);
    if (err == ESP_OK && sendLength && esp_http_client_write(client, json, sendLength) != sendLength) err = ESP_FAIL;
    if (err == ESP_OK && esp_http_client_fetch_headers(client) < 0) err = ESP_FAIL;
    if (err == ESP_OK) {
        result = esp_http_client_get_status_code(client);
        size_t total = 0;
        if (body && max) {
            for (;;) {
                const int n = esp_http_client_read(client, (char *)body + total, max - 1 - total);
                if (n <= 0) break;
                total += n;
                if (total >= max - 1) { result = -ESP_ERR_INVALID_SIZE; break; }
            }
            body[total] = 0;
        }
        if (length) *length = total;
    } else {
        result = -err;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

const char *str(const cJSON *object, const char *key, const char *fallback = "---") {
    const cJSON *item = cJSON_GetObjectItem(object, key);
    return cJSON_IsString(item) ? item->valuestring : fallback;
}

double num(const cJSON *object, const char *key, double fallback = 0) {
    const cJSON *item = cJSON_GetObjectItem(object, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

void wallClock(char *out, size_t size) {
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    if (local.tm_year < 120) { strlcpy(out, "--:--:--", size); return; }
    strftime(out, size, "%H:%M:%S", &local);
}

// Same keys HMI_MCU read (HMI_MCU/src/main.cpp TaskNetwork).
void parseStatus(const char *text) {
    cJSON *doc = cJSON_Parse(text);
    if (!doc) return;
    PcStatus next;
    xSemaphoreTake(statusLock, portMAX_DELAY);
    next = status;
    xSemaphoreGive(statusLock);
    char seen[9];
    wallClock(seen, sizeof(seen));
    for (int i = 0; i < PC_BOARDS; ++i) {
        const cJSON *b = cJSON_GetObjectItem(doc, kBoardKeys[i]);
        PcBoardState &s = next.board[i];
        s.online = !strcmp(str(b, "state", ""), "Online");
        s.rssi = (int)num(b, "rssi");
        s.temp = (float)num(b, "temp");
        strlcpy(s.ip, str(b, "ip"), sizeof(s.ip));
        if (s.online) strlcpy(s.lastSeen, seen, sizeof(s.lastSeen));
    }
    const cJSON *robot = cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "robot3axismcu"), "state_data");
    next.robotX = (float)num(robot, "x_deg");
    next.robotY = (float)num(robot, "y_mm");
    next.robotZ = (float)num(robot, "z_mm");
    strlcpy(next.robotGrab, str(robot, "grab_state"), sizeof(next.robotGrab));
    next.robotSpeed[0] = (int)num(robot, "x_speed", 100);
    next.robotSpeed[1] = (int)num(robot, "y_speed", 700);
    next.robotSpeed[2] = (int)num(robot, "z_speed", 1000);

    const cJSON *cam = cJSON_GetObjectItem(doc, "imagesendermcu");
    const cJSON *cfg = cJSON_GetObjectItem(cam, "config");
    strlcpy(next.camSensor, str(cfg, "camera_model"), sizeof(next.camSensor));
    strlcpy(next.camRes, str(cfg, "resolution"), sizeof(next.camRes));
    next.camQuality = (int)num(cfg, "jpeg_quality");
    const cJSON *analysis = cJSON_GetObjectItem(cam, "last_analysis");
    strlcpy(next.aiObject, str(analysis, "object", "None"), sizeof(next.aiObject));
    strlcpy(next.aiCategory, str(analysis, "category", "None"), sizeof(next.aiCategory));
    strlcpy(next.aiModel, str(cam, "model_name"), sizeof(next.aiModel));

    // SoundMCU was removed; voice I/O lives on the ESP32-S3 ControlMCU (ARD_MAIN app.py get_status()).
    const cJSON *voice = cJSON_GetObjectItem(doc, "controlmcu");
    strlcpy(next.soundMode, str(voice, "voice_mode"), sizeof(next.soundMode));
    strlcpy(next.soundStatus, str(voice, "status"), sizeof(next.soundStatus));
    strlcpy(next.controlMode, str(voice, "mode"), sizeof(next.controlMode));

    const cJSON *pending = cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "hmi_mcu"), "pending_action");
    next.approvalPending = cJSON_IsObject(pending);
    if (next.approvalPending) {
        const cJSON *id = cJSON_GetObjectItem(pending, "id");
        if (cJSON_IsNumber(id)) snprintf(next.approvalId, sizeof(next.approvalId), "%.0f", id->valuedouble);
        else strlcpy(next.approvalId, str(pending, "id", ""), sizeof(next.approvalId));
        strlcpy(next.approvalDesc, str(pending, "desc", ""), sizeof(next.approvalDesc));
    }
    next.reachable = true;
    next.updatedMs = nowMs();
    cJSON_Delete(doc);
    xSemaphoreTake(statusLock, portMAX_DELAY);
    status = next;
    xSemaphoreGive(statusLock);
}

void poll(char *buffer) {
    const int code = http("/api/status", HTTP_METHOD_GET, nullptr, 1000, (uint8_t *)buffer, kStatusMax);
    if (code == 200) { parseStatus(buffer); return; }
    xSemaphoreTake(statusLock, portMAX_DELAY);
    status.reachable = false;
    xSemaphoreGive(statusLock);
}

void heartbeat() {
    char ip[16], body[160];
    ard_network_local_ip(ip, sizeof(ip));
    snprintf(body, sizeof(body), "{\"source\":\"HMI_MCU\",\"rssi\":%d,\"ip\":\"%s\",\"board\":\"JC1060P470C\"}",
             ard_network_rssi(), ip);
    http("/api/heartbeat", HTTP_METHOD_POST, body, 500);
}

void send(const Outgoing &item) {
    cJSON *doc = cJSON_CreateObject();
    const char *path;
    if (item.kind == Outgoing::APPROVAL) {
        cJSON_AddStringToObject(doc, "id", item.text);
        cJSON_AddBoolToObject(doc, "approve", item.approve);
        path = "/api/hmi/approve";
    } else {
        cJSON_AddStringToObject(doc, "command", item.text);
        path = kCommandPaths[item.board];
    }
    char *json = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!json) return;
    const int code = http(path, HTTP_METHOD_POST, json, 1000);
    if (code != 200) ESP_LOGW(TAG, "POST %s -> %d", path, code);
    cJSON_free(json);
}

void setImageError(const char *message) {
    xSemaphoreTake(imageLock, portMAX_DELAY);
    strlcpy(image.error, message, sizeof(image.error));
    image.sequence++;
    xSemaphoreGive(imageLock);
}

// /api/hmi/last_image -> hardware JPEG decoder (ESP32-P4) -> RGB565 for the Cam tab.
void fetchImage() {
    size_t length = 0;
    const int code = http("/api/hmi/last_image", HTTP_METHOD_GET, nullptr, 3000, jpegBuffer, kJpegMax, &length);
    char message[40];
    if (code != 200 || !length) { snprintf(message, sizeof(message), "HTTP error %d", code); setImageError(message); return; }
    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(jpegBuffer, length, &info) != ESP_OK) { setImageError("Not a JPEG"); return; }
    // The decoder writes whole 16x16 MCUs.
    const uint32_t width = (info.width + 15) & ~15u, height = (info.height + 15) & ~15u;
    const size_t needed = width * height * 2;
    xSemaphoreTake(imageLock, portMAX_DELAY);
    if (needed > pixelsSize) {
        if (pixels) heap_caps_free(pixels);
        jpeg_decode_memory_alloc_cfg_t memory = {};
        memory.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
        size_t allocated = 0;
        pixels = (uint8_t *)jpeg_alloc_decoder_mem(needed, &memory, &allocated);
        pixelsSize = pixels ? allocated : 0;
    }
    esp_err_t err = ESP_ERR_NO_MEM;
    uint32_t out = 0;
    if (pixels) {
        jpeg_decode_cfg_t cfg = {};
        cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
        cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR; // little-endian RGB565, as LVGL expects
        err = jpeg_decoder_process(jpeg, &cfg, jpegBuffer, length, pixels, pixelsSize, &out);
    }
    if (err == ESP_OK) {
        image.pixels = pixels;
        image.width = (uint16_t)width;
        image.height = (uint16_t)height;
        image.error[0] = 0;
    } else {
        image.pixels = nullptr;
        snprintf(image.error, sizeof(image.error), "Decode error: %s", esp_err_to_name(err));
    }
    image.sequence++;
    xSemaphoreGive(imageLock);
    ESP_LOGI(TAG, "Image %ux%u (%u bytes JPEG): %s", (unsigned)info.width, (unsigned)info.height,
             (unsigned)length, esp_err_to_name(err));
}

void linkTask(void *) {
    char *buffer = (char *)heap_caps_malloc(kStatusMax, MALLOC_CAP_SPIRAM);
    uint32_t lastPoll = 0, lastHeartbeat = 0;
    for (;;) {
        Outgoing item;
        // Commands and approvals first: they are what the operator is waiting on.
        while (xQueueReceive(outgoing, &item, 0) == pdTRUE) send(item);
        if (imageRequested.exchange(false)) fetchImage();
        const uint32_t now = nowMs();
        if (ard_network_connected()) {
            if (buffer && now - lastPoll >= kPollMs) { lastPoll = now; poll(buffer); }
            if (now - lastHeartbeat >= kHeartbeatMs) { lastHeartbeat = now; heartbeat(); }
        }
        xQueuePeek(outgoing, &item, pdMS_TO_TICKS(50)); // wake early when something is queued
    }
}

} // namespace

esp_err_t pc_link_start() {
    statusLock = xSemaphoreCreateMutex();
    imageLock = xSemaphoreCreateMutex();
    outgoing = xQueueCreate(8, sizeof(Outgoing));
    jpeg_decode_memory_alloc_cfg_t input = {};
    input.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER; // DMA-aligned, as the decoder requires
    size_t allocated = 0;
    jpegBuffer = (uint8_t *)jpeg_alloc_decoder_mem(kJpegMax, &input, &allocated);
    if (!statusLock || !imageLock || !outgoing || !jpegBuffer) return ESP_ERR_NO_MEM;
    strlcpy(image.error, "No image yet", sizeof(image.error));
    jpeg_decode_engine_cfg_t engine = {};
    engine.timeout_ms = 200;
    esp_err_t err = jpeg_new_decoder_engine(&engine, &jpeg);
    if (err != ESP_OK) return err;
    return xTaskCreatePinnedToCore(linkTask, "pc_link", 6144, nullptr, 3, &worker, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

PcStatus pc_status() {
    PcStatus copy = {};
    if (statusLock && xSemaphoreTake(statusLock, pdMS_TO_TICKS(20)) == pdTRUE) {
        copy = status;
        xSemaphoreGive(statusLock);
    }
    return copy;
}

bool pc_send_command(PcBoard board, const char *command) {
    Outgoing item = {Outgoing::COMMAND, board, false, {}};
    strlcpy(item.text, command, sizeof(item.text));
    return outgoing && xQueueSend(outgoing, &item, 0) == pdTRUE;
}

void pc_send_approval(const char *id, bool approve) {
    Outgoing item = {Outgoing::APPROVAL, PC_DRIVE, approve, {}};
    strlcpy(item.text, id, sizeof(item.text));
    if (outgoing) xQueueSend(outgoing, &item, 0);
    xSemaphoreTake(statusLock, portMAX_DELAY);
    status.approvalPending = false; // hide the popup until the next poll says otherwise
    xSemaphoreGive(statusLock);
}

void pc_request_image() { imageRequested = true; }

bool pc_image_lock(PcImage *out, uint32_t timeoutMs) {
    if (!imageLock || xSemaphoreTake(imageLock, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return false;
    *out = image;
    return true;
}

void pc_image_unlock() { xSemaphoreGive(imageLock); }
