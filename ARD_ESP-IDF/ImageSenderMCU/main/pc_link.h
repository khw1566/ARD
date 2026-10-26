#pragma once
// HTTP link to the PC backend (port 5000): config poll, heartbeat, log, /upload.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char resolution[8];
    int quality;
    bool tcp_mode; // transmission_mode "TCP" -> HTTP POST /upload, else UDP protocol v2
} pc_config_t;

typedef bool (*pc_capture_cb_t)(const char *source, uint32_t capture_id);
typedef void (*pc_reset_cb_t)(void);

// Creates the locks behind pc_link_get_config/log/upload. Call before any task uses them.
void pc_link_init(void);
// Starts the poll/heartbeat task. on_capture runs when the PC queues "capture".
void pc_link_start(pc_capture_cb_t on_capture, pc_reset_cb_t on_reset);
void pc_link_get_config(pc_config_t *out);
// Thread-safe, nonblocking single-slot diagnostic log (newer messages replace older ones).
bool pc_link_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
bool pc_link_upload_jpeg(const uint8_t *jpeg, size_t len);
bool pc_link_upload_capture(const uint8_t *jpeg, size_t len, uint32_t capture_id);
