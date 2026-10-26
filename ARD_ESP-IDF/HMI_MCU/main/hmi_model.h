#pragma once
// Everything the screen shows, as typed fields. The network task fills it from
// /api/status; the UI task reads a snapshot. Portable C (shared with the PC simulator).
#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { DEV_DRIVE, DEV_ROBOT, DEV_CAM, DEV_SOUND, DEV_COUNT };

typedef struct {
    bool online;
    int rssi;
    float temp;
    char ip[16];
    char last_seen[9]; // HH:MM:SS of the last poll that reported the board online
} hmi_device_t;

typedef struct {
    // This board
    bool wifi_ok;
    bool server_ok;     // last /api/status poll succeeded
    int wifi_rssi;
    char server[65];

    hmi_device_t dev[DEV_COUNT];

    // DriveMCU + drive_settings
    bool has_tof;
    int tof_mm;
    bool pid_mode;
    int pid_target;     // mm
    int pid_min, pid_max; // Z safety limits (ToF mm) from the PC, 70..120 by default
    int z_step;
    int hb_ms;
    int i2c_hz;

    // Robot3AxisMCU state_data
    float robot_x_deg, robot_y_mm, robot_z_mm;
    char grab[12];
    int robot_speed[3]; // x, y, z steps/s

    // ImageSenderMCU
    char cam_sensor[12];
    char cam_res[8];
    int cam_quality;
    char ai_object[48];
    char ai_category[48];
    char ai_model[48];

    // ControlMCU (ESP32-S3): voice mode/state + board mode
    char sound_mode[16];
    char sound_status[24];
    char ctrl_mode[4];

    // AI action waiting for the operator
    bool approval;
    char approval_id[24];
    char approval_desc[128];

    uint32_t revision;  // bumped on every change the screen should show
} hmi_model_t;

void hmi_model_defaults(hmi_model_t *m);

#ifdef __cplusplus
}
#endif
