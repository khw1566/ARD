#pragma once
// TCP 5003: capture <id> -> ACK:CAPTURE_START,<id> / ERR:BUSY;
// reset -> ACK:RESET_PENDING; ping -> ACK:PONG with busy/reset/ready and counters.
// Legacy c/capture remains supported without a capture ID.
// Lets the PC trigger a capture immediately instead of waiting for the 3 s poll.
#include <stdbool.h>
#include <stdint.h>
typedef bool (*cmd_capture_cb_t)(const char *source, uint32_t capture_id);
typedef void (*cmd_reset_cb_t)(void);
typedef void (*cmd_stats_cb_t)(char *out, int size);

void cmd_server_start(cmd_capture_cb_t on_capture, cmd_reset_cb_t on_reset, cmd_stats_cb_t stats);
