#pragma once
// TCP 5003 command server: "c"/"capture" -> ACK:CAPTURE_START, "ping" -> ACK:PONG.
// Lets the PC trigger a capture immediately instead of waiting for the 3 s poll.
typedef void (*cmd_capture_cb_t)(const char *source);
typedef void (*cmd_stats_cb_t)(char *out, int size);

void cmd_server_start(cmd_capture_cb_t on_capture, cmd_stats_cb_t stats);
