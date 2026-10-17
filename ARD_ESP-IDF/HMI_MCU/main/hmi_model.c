#include "hmi_model.h"
#include <string.h>

void hmi_model_defaults(hmi_model_t *m) {
    memset(m, 0, sizeof(*m));
    m->wifi_rssi = -127;
    for (int i = 0; i < DEV_COUNT; ++i) strcpy(m->dev[i].last_seen, "--:--:--");
    // Same defaults as the PC server's drive_settings / robot3axis_state.
    m->pid_target = 145;
    m->z_step = 50;
    m->hb_ms = 1000;
    m->i2c_hz = 100000;
    m->robot_speed[0] = 100;
    m->robot_speed[1] = 700;
    m->robot_speed[2] = 1000;
    strcpy(m->grab, "---");
    strcpy(m->cam_sensor, "---");
    strcpy(m->cam_res, "---");
    strcpy(m->ai_object, "None");
    strcpy(m->ai_category, "None");
    strcpy(m->ai_model, "---");
    strcpy(m->sound_mode, "---");
    strcpy(m->sound_status, "---");
    strcpy(m->ctrl_mode, "---");
}
