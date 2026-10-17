// ESP-IDF side of hmi_port.h: model storage, PC server HTTP traffic, preview images.
#include "hmi_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "ard_network.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hmi_port.h"
#include "hmi_ui.h"
#include "jpeg.h"
#include "sdkconfig.h"

static const char *TAG = "HMI_NET";

#define SERVER_PORT        5000
#define STATUS_BUF_SIZE    (16 * 1024)
#define IMAGE_MAX_BYTES    (40 * 1024) // same safety limit as the Arduino build
#define HEARTBEAT_MS       10000
#define IMAGE_PERIOD_MS    3000
#define OPTIMISTIC_HOLD_MS 4000
#define COMMAND_TIMEOUT_MS 1500
#define STATUS_TIMEOUT_MS  1500

typedef enum { REQ_COMMAND, REQ_APPROVAL, REQ_REFRESH, REQ_IMAGE } req_kind_t;

typedef struct {
    req_kind_t kind;
    hmi_target_t target;
    bool approve;
    char text[40];
} net_req_t;

static SemaphoreHandle_t model_lock, image_lock;
static hmi_model_t model;
static int64_t hold_until_us;
static QueueHandle_t requests;
static char *status_buf;

static gfx_image_t preview;
static uint32_t preview_seq;
static const char *preview_error;
static volatile bool preview_wanted;

// ---- hmi_port.h ---------------------------------------------------------------
uint32_t hmi_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool time_synced(void) { return time(NULL) > 1700000000; }

bool hmi_clock(char *out, size_t size) {
    if (!time_synced()) return false;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(out, size, "%H:%M", &tm);
    return true;
}

void hmi_model_get(hmi_model_t *out) {
    xSemaphoreTake(model_lock, portMAX_DELAY);
    *out = model;
    xSemaphoreGive(model_lock);
}

void hmi_model_edit(void (*edit)(hmi_model_t *m, void *arg), void *arg) {
    xSemaphoreTake(model_lock, portMAX_DELAY);
    edit(&model, arg);
    model.revision++;
    hold_until_us = esp_timer_get_time() + OPTIMISTIC_HOLD_MS * 1000LL;
    xSemaphoreGive(model_lock);
}

static void enqueue(const net_req_t *req) {
    // Drop rather than block the UI when the server is unreachable and the queue is full.
    if (xQueueSend(requests, req, 0) != pdTRUE) ESP_LOGW(TAG, "Request queue full; dropped %s", req->text);
}

void hmi_send_command(hmi_target_t target, const char *command) {
    net_req_t req = {.kind = REQ_COMMAND, .target = target};
    strlcpy(req.text, command, sizeof(req.text));
    enqueue(&req);
}

void hmi_send_approval(const char *id, bool approve) {
    net_req_t req = {.kind = REQ_APPROVAL, .approve = approve};
    strlcpy(req.text, id, sizeof(req.text));
    enqueue(&req);
}

void hmi_request_refresh(void) {
    net_req_t req = {.kind = REQ_REFRESH};
    enqueue(&req);
}

void hmi_image_want(bool wanted) {
    preview_wanted = wanted;
    if (wanted) {
        net_req_t req = {.kind = REQ_IMAGE};
        enqueue(&req);
    }
}

const gfx_image_t *hmi_image_acquire(uint32_t *seq, const char **error) {
    xSemaphoreTake(image_lock, portMAX_DELAY);
    *seq = preview_seq;
    *error = preview_error;
    return preview.px ? &preview : NULL;
}

void hmi_image_release(void) { xSemaphoreGive(image_lock); }

// ---- HTTP -------------------------------------------------------------------------
// Returns the HTTP status (or -1) and copies up to cap-1 body bytes into resp (NUL-terminated).
static int http_call(esp_http_client_method_t method, const char *path, const char *json, char *resp, int cap,
                     int *resp_len, int timeout_ms) {
    char host[65], url[128];
    ard_network_server(host, sizeof(host));
    snprintf(url, sizeof(url), "http://%s:%d%s", host, SERVER_PORT, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .method = method,
        .timeout_ms = timeout_ms,
        .disable_auto_redirect = true,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;
    int body_len = json ? (int)strlen(json) : 0;
    if (json) esp_http_client_set_header(client, "Content-Type", "application/json");
    int status = -1, total = 0;
    if (esp_http_client_open(client, body_len) == ESP_OK &&
        (!body_len || esp_http_client_write(client, json, body_len) == body_len) &&
        esp_http_client_fetch_headers(client) >= 0) {
        status = esp_http_client_get_status_code(client);
        while (resp && total < cap - 1) {
            int n = esp_http_client_read(client, resp + total, cap - 1 - total);
            if (n <= 0) break;
            total += n;
        }
        // A body that does not fit is reported as truncated (-2) instead of being half-parsed.
        if (resp && total == cap - 1 && !esp_http_client_is_complete_data_received(client)) status = -2;
    }
    if (resp) resp[total] = 0;
    if (resp_len) *resp_len = total;
    esp_http_client_cleanup(client);
    return status;
}

static int post_json(const char *path, cJSON *body, int timeout_ms) {
    char *text = cJSON_PrintUnformatted(body);
    if (!text) return -1;
    int status = http_call(HTTP_METHOD_POST, path, text, NULL, 0, NULL, timeout_ms);
    cJSON_free(text);
    return status;
}

// ---- Status parsing -------------------------------------------------------------
static void copy_str(const cJSON *obj, const char *key, char *dst, size_t size, const char *fallback) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v) && v->valuestring) strlcpy(dst, v->valuestring, size);
    else if (cJSON_IsNumber(v)) snprintf(dst, size, "%g", v->valuedouble);
    else if (fallback) strlcpy(dst, fallback, size);
}

static double get_num(const cJSON *obj, const char *key, double fallback) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

static const cJSON *obj(const cJSON *parent, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(parent, key);
    return cJSON_IsObject(v) ? v : NULL;
}

static bool is_online(const cJSON *dev) {
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(dev, "state");
    return cJSON_IsString(state) && !strcmp(state->valuestring, "Online");
}

static void parse_status(const cJSON *root, hmi_model_t *m, bool hold) {
    static const char *keys[DEV_COUNT] = {"drivemcu", "robot3axismcu", "imagesendermcu", "controlmcu"};
    char stamp[9] = "";
    if (time_synced()) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm);
    }
    for (int i = 0; i < DEV_COUNT; ++i) {
        const cJSON *d = obj(root, keys[i]);
        hmi_device_t *dev = &m->dev[i];
        dev->online = d && is_online(d);
        dev->rssi = (int)get_num(d, "rssi", 0);
        dev->temp = (float)get_num(d, "temp", 0);
        copy_str(d, "ip", dev->ip, sizeof(dev->ip), "---");
        if (dev->online && stamp[0]) strlcpy(dev->last_seen, stamp, sizeof(dev->last_seen));
    }

    const cJSON *drive = obj(root, "drivemcu");
    const cJSON *tof = cJSON_GetObjectItemCaseSensitive(drive, "tof_dist");
    m->has_tof = cJSON_IsNumber(tof);
    m->tof_mm = m->has_tof ? (int)tof->valuedouble : 0;

    const cJSON *robot = obj(obj(root, "robot3axismcu"), "state_data");
    m->robot_x_deg = (float)get_num(robot, "x_deg", 0);
    m->robot_y_mm = (float)get_num(robot, "y_mm", 0);
    m->robot_z_mm = (float)get_num(robot, "z_mm", 0);
    copy_str(robot, "grab_state", m->grab, sizeof(m->grab), "---");

    const cJSON *cam = obj(root, "imagesendermcu");
    const cJSON *analysis = obj(cam, "last_analysis");
    copy_str(analysis, "object", m->ai_object, sizeof(m->ai_object), "None");
    copy_str(analysis, "category", m->ai_category, sizeof(m->ai_category), "None");
    copy_str(cam, "model_name", m->ai_model, sizeof(m->ai_model), "---");

    const cJSON *sound = obj(root, "controlmcu");  // voice I/O lives on the ESP32-S3 ControlMCU
    copy_str(sound, "voice_mode", m->sound_mode, sizeof(m->sound_mode), "---");
    copy_str(sound, "status", m->sound_status, sizeof(m->sound_status), "---");

    // The server reports ControlMCU's last mode even when the board is gone.
    const cJSON *control = obj(root, "controlmcu");
    if (control && is_online(control)) copy_str(control, "mode", m->ctrl_mode, sizeof(m->ctrl_mode), "---");
    else strlcpy(m->ctrl_mode, "---", sizeof(m->ctrl_mode));

    const cJSON *action = obj(obj(root, "hmi_mcu"), "pending_action");
    m->approval = action != NULL;
    if (action) {
        copy_str(action, "id", m->approval_id, sizeof(m->approval_id), "");
        copy_str(action, "desc", m->approval_desc, sizeof(m->approval_desc), "");
    } else {
        m->approval_id[0] = m->approval_desc[0] = 0;
    }

    // Settings the operator can change from this screen keep the local value while
    // an optimistic edit is fresh, so an older poll cannot flip the button back.
    if (hold) return;
    const cJSON *ds = obj(root, "drive_settings");
    if (ds) {
        const cJSON *pid = cJSON_GetObjectItemCaseSensitive(ds, "pid_mode");
        if (cJSON_IsBool(pid)) m->pid_mode = cJSON_IsTrue(pid);
        m->pid_target = (int)get_num(ds, "pid_target_tof", m->pid_target);
        m->z_step = (int)get_num(ds, "z_step", m->z_step);
        m->hb_ms = (int)get_num(ds, "hb_interval_ms", m->hb_ms);
        m->i2c_hz = (int)get_num(ds, "i2c_clock_hz", m->i2c_hz);
    }
    m->robot_speed[0] = (int)get_num(robot, "x_speed", 100);
    m->robot_speed[1] = (int)get_num(robot, "y_speed", 700);
    m->robot_speed[2] = (int)get_num(robot, "z_speed", 1000);
    const cJSON *cfg = obj(cam, "config");
    if (cfg) {
        copy_str(cfg, "camera_model", m->cam_sensor, sizeof(m->cam_sensor), "---");
        copy_str(cfg, "resolution", m->cam_res, sizeof(m->cam_res), "---");
        m->cam_quality = (int)get_num(cfg, "jpeg_quality", 0);
    }
}

// Commits a new model and bumps the revision only when something visible changed.
static void commit(void (*update)(hmi_model_t *m, void *arg), void *arg) {
    xSemaphoreTake(model_lock, portMAX_DELAY);
    hmi_model_t next = model;
    update(&next, arg);
    next.revision = model.revision;
    if (memcmp(&next, &model, sizeof(next)) != 0) {
        next.revision++;
        model = next;
    }
    xSemaphoreGive(model_lock);
}

typedef struct {
    const cJSON *root;
    bool ok;
} poll_result_t;

static void apply_poll(hmi_model_t *m, void *arg) {
    const poll_result_t *r = arg;
    m->wifi_ok = ard_network_connected();
    // Only the bar count is shown; avoid a redraw for every 1 dBm wobble.
    int rssi = ard_network_rssi();
    m->wifi_rssi = rssi > -70 ? -60 : rssi > -80 ? -75 : rssi > -90 ? -85 : -127;
    ard_network_server(m->server, sizeof(m->server));
    m->server_ok = r->ok;
    if (r->root) parse_status(r->root, m, esp_timer_get_time() < hold_until_us);
}

static void poll_status(void) {
    poll_result_t r = {0};
    if (ard_network_connected()) {
        int len = 0;
        int status = http_call(HTTP_METHOD_GET, "/api/status", NULL, status_buf, STATUS_BUF_SIZE, &len,
                               STATUS_TIMEOUT_MS);
        if (status == 200) {
            cJSON *root = cJSON_ParseWithLength(status_buf, len);
            r.root = root;
            r.ok = root != NULL;
            if (!root) ESP_LOGW(TAG, "Status JSON parse failed (%d bytes)", len);
            commit(apply_poll, &r);
            cJSON_Delete(root);
            return;
        }
        ESP_LOGW(TAG, "Status poll failed: %d", status);
    }
    commit(apply_poll, &r);
}

static void send_heartbeat(void) {
    if (!ard_network_connected()) return;
    char ip[16];
    ard_network_local_ip(ip, sizeof(ip));
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "source", "HMI_MCU");
    cJSON_AddNumberToObject(body, "rssi", ard_network_rssi());
    cJSON_AddStringToObject(body, "ip", ip);
    // The classic ESP32 has no supported die temperature sensor in ESP-IDF; the
    // server defaults "temp" to 0 when it is missing.
    post_json("/api/heartbeat", body, 1000);
    cJSON_Delete(body);
}

static void send_command(const net_req_t *req) {
    static const char *paths[] = {"/api/drivemcu/command", "/api/robot3axis/command",
                                  "/api/imagesendermcu/command", "/api/controlmcu/command"};
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "command", req->text);
    int status = post_json(paths[req->target], body, COMMAND_TIMEOUT_MS);
    cJSON_Delete(body);
    if (status != 200) ESP_LOGW(TAG, "Command '%s' -> %s: HTTP %d", req->text, paths[req->target], status);
    else ESP_LOGI(TAG, "Command '%s' sent", req->text);
}

static void send_approval(const net_req_t *req) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "id", req->text);
    cJSON_AddBoolToObject(body, "approve", req->approve);
    int status = post_json("/api/hmi/approve", body, COMMAND_TIMEOUT_MS);
    cJSON_Delete(body);
    ESP_LOGI(TAG, "Approval %s for %s: HTTP %d", req->approve ? "OK" : "NO", req->text, status);
}

static void set_preview(gfx_image_t *img, const char *error) {
    xSemaphoreTake(image_lock, portMAX_DELAY);
    free(preview.px);
    preview = img ? *img : (gfx_image_t){0};
    preview_error = error;
    preview_seq++;
    xSemaphoreGive(image_lock);
}

static void fetch_preview(void) {
    if (!ard_network_connected()) {
        set_preview(NULL, "Wi-Fi offline");
        return;
    }
    uint8_t *buf = malloc(IMAGE_MAX_BYTES + 1);
    if (!buf) {
        set_preview(NULL, "Memory full");
        return;
    }
    int len = 0;
    int status = http_call(HTTP_METHOD_GET, "/api/hmi/last_image", NULL, (char *)buf, IMAGE_MAX_BYTES + 1, &len,
                           2000);
    gfx_image_t img;
    if (status == 404) set_preview(NULL, "No image yet");
    else if (status == -2) set_preview(NULL, "Image too large");
    else if (status != 200) set_preview(NULL, "HTTP error");
    else if (jpeg_decode(buf, len, HMI_PREVIEW_W, HMI_PREVIEW_H, &img) != ESP_OK) set_preview(NULL, "Decode error");
    else set_preview(&img, NULL);
    free(buf);
}

static void net_task(void *arg) {
    int64_t next_poll = 0, next_hb = 0, next_image = 0;
    bool was_connected = false;
    while (true) {
        int64_t now = esp_timer_get_time() / 1000;
        int64_t due = next_poll < next_hb ? next_poll : next_hb;
        if (preview_wanted && next_image < due) due = next_image;
        net_req_t req;
        TickType_t wait = due > now ? pdMS_TO_TICKS(due - now) : 0;
        if (xQueueReceive(requests, &req, wait) == pdTRUE) {
            switch (req.kind) {
            case REQ_COMMAND:
                if (ard_network_connected()) send_command(&req);
                // Show the server's view of the change soon after the command.
                next_poll = esp_timer_get_time() / 1000 + 300;
                break;
            case REQ_APPROVAL:
                if (ard_network_connected()) send_approval(&req);
                next_poll = esp_timer_get_time() / 1000 + 300;
                break;
            case REQ_REFRESH: next_poll = 0; break;
            case REQ_IMAGE: next_image = 0; break;
            }
            continue; // drain the queue before polling
        }
        now = esp_timer_get_time() / 1000;
        bool connected = ard_network_connected();
        if (connected && !was_connected) next_poll = next_hb = 0; // report in as soon as Wi-Fi is up
        was_connected = connected;
        if (now >= next_poll) {
            poll_status();
            next_poll = esp_timer_get_time() / 1000 + CONFIG_HMI_STATUS_POLL_MS;
        }
        if (now >= next_hb) {
            send_heartbeat();
            next_hb = esp_timer_get_time() / 1000 + HEARTBEAT_MS;
        }
        if (preview_wanted && now >= next_image) {
            fetch_preview();
            next_image = esp_timer_get_time() / 1000 + IMAGE_PERIOD_MS;
        }
    }
}

esp_err_t hmi_net_start(void) {
    hmi_model_defaults(&model);
    model_lock = xSemaphoreCreateMutex();
    image_lock = xSemaphoreCreateMutex();
    requests = xQueueCreate(8, sizeof(net_req_t));
    status_buf = malloc(STATUS_BUF_SIZE);
    if (!model_lock || !image_lock || !requests || !status_buf) return ESP_ERR_NO_MEM;
    // Network I/O lives on core 0 with the Wi-Fi stack; the UI owns core 1.
    if (xTaskCreatePinnedToCore(net_task, "hmi_net", 6144, NULL, 4, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
