// ARD HMI v3 | Portrait 240x320 | layout carried over from the Arduino v2.2 firmware.
#include "hmi_ui.h"
#include <stdio.h>
#include <string.h>
#include "hmi_port.h"

// Optimistic OpenLoop "STP" buttons. Off by default: the PC server forwards
// "mzs+N" to DriveMCU, which reads it as a new Z steps/mm calibration value.
#ifndef HMI_Z_STEP_BUTTONS
#define HMI_Z_STEP_BUTTONS 0
#endif
// ToF-based rail PID control (PID on/off, target). Locked until the ToF PID loop is
// validated; the Z Rail page then shows ToF and the mode read-only and keeps HOME.
#ifndef HMI_RAIL_PID
#define HMI_RAIL_PID 0
#endif

// ---- Layout -----------------------------------------------------------------
#define HDR_H  28
#define TAB_H  30
#define SUB_H  26
#define CONT_Y (HDR_H + TAB_H + SUB_H)
#define CONT_H 160
#define RBTN_W 62
#define INFO_W (HMI_W - RBTN_W)
#define BOT_Y  (CONT_Y + CONT_H)
#define BOT_H  (HMI_H - BOT_Y)
#define NAV_W  42
#define NAV_X  (HMI_W - NAV_W * 2 - 4)

// ---- Colours (RGB565 as tuned for the inverted CYD panel) -------------------
#define C_BG    0xFFFF
#define C_HDR   0xEF7D
#define C_TABA  0xD67A
#define C_ACC   0x0210
#define C_ACC2  0x0410
#define C_BTN   0xE71C
#define C_INFO  0xF7BE
#define C_PANEL 0xEF7D
#define C_SEP   0xD6BA
#define C_ON    0x0500
#define C_OFF   0xA000
#define C_DIM   0x4208
#define C_TEXT  0x2104
#define C_CYAN  0x07FF
#define C_AMBER 0xB400 // was yellow 0xFFE0, unreadable on the light bottom panel
#define C_GREY  0xBDF7

#define TAB_COUNT 4
static const char *const TAB_NAMES[TAB_COUNT] = {"Drive", "Robot", "Cam", "Ctrl"};
static const char *const DEV_NAMES[TAB_COUNT] = {"DriveMCU", "Robot3Axis", "CamMCU", "ControlMCU"};
static const int PAGE_COUNT[TAB_COUNT] = {4, 4, 4, 1};
static const char *const PAGE_TITLES[TAB_COUNT][4] = {
    {"Network", "Rotate Roll/Yaw", "Jog X/Y", "Z Rail"},
    {"Network", "Axis Jog", "Gripper", "Max Speed"},
    {"Network", "Capture", "Camera Config", "AI Model"},
    {"Voice", "", "", ""},
};

// DriveMCU rejects heartbeat intervals below 250 ms (the old 200 ms entry did nothing).
static const int HB_MS[] = {250, 500, 1000, 2000};
static const int I2C_HZ[] = {50000, 100000, 400000};
static const char *const I2C_NAMES[] = {"LO", "MD", "HI"};
// Same cycles as the PC server's c_sens / c_res / c_qlt handlers.
static const char *const CAM_SENSORS[] = {"OV2640", "OV3660", "OV5640"};
static const char *const CAM_RES[] = {"VGA", "SVGA", "XGA", "SXGA", "UXGA"};
static const char *const CAM_RES_PX[] = {"640x480", "800x600", "1024x768", "1280x1024", "1600x1200"};
static const int CAM_QUALITY[] = {5, 10, 20, 40};
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

#define PID_MIN 145
#define PID_MAX 175
#define PID_STEP 5
#define Z_STEP_DELTA 10

// ---- Buttons ------------------------------------------------------------------
typedef enum {
    BA_SEND,     // send cmd as is
    BA_HB,       // cycle DriveMCU heartbeat interval
    BA_I2C,      // cycle DriveMCU I2C clock
    BA_PID,      // toggle rail PID mode
    BA_TARGET,   // PID target +/- arg
    BA_ZSTEP,    // OpenLoop step +/- arg
    BA_SPEED,    // robot axis max speed (arg = axis * 2 + down)
    BA_VIEW,     // camera preview on/off (local only)
    BA_CAM_SENS,
    BA_CAM_RES,
    BA_CAM_QUAL,
    BA_REFRESH,  // send cmd, then poll status at once
    BA_NONE,     // disabled
} btn_action_t;

typedef struct {
    char label[24];
    char cmd[24];
    uint16_t color;
    btn_action_t action;
    int arg;
} btn_t;

#define MAX_BTNS 6

// ---- UI state -----------------------------------------------------------------
typedef enum { PRESS_NONE, PRESS_BTN, PRESS_PREV, PRESS_NEXT, PRESS_OK, PRESS_NO } press_t;

static struct {
    gfx_target_t target;
    bool started;
    int tab;
    int page[TAB_COUNT];
    bool cam_view;
    press_t press;
    int press_index;
    uint32_t press_until;
    char answered_id[24];
    uint32_t answered_at;
    hmi_model_t m;               // snapshot used by the scene
    uint32_t drawn_revision;
    char drawn_clock[8];
    uint32_t drawn_image_seq;
    bool dirty;
    // splash
    const gfx_image_t *logo;
    const char *splash_status;
} ui;

static int find_int(const int *list, int n, int v) {
    for (int i = 0; i < n; ++i)
        if (list[i] == v) return i;
    return -1;
}

static int find_str(const char *const *list, int n, const char *v) {
    for (int i = 0; i < n; ++i)
        if (!strcmp(list[i], v)) return i;
    return -1;
}

static void set_btn(btn_t *b, const char *label, const char *cmd, uint16_t color, btn_action_t action, int arg) {
    snprintf(b->label, sizeof(b->label), "%s", label);
    snprintf(b->cmd, sizeof(b->cmd), "%s", cmd);
    b->color = color;
    b->action = action;
    b->arg = arg;
}

static int build_buttons(const hmi_model_t *m, int tab, int page, btn_t *b) {
    char label[24], cmd[24];
    switch (tab * 4 + page) {
    case 0: { // Drive / Network
        int hb = find_int(HB_MS, COUNT(HB_MS), m->hb_ms);
        int i2c = find_int(I2C_HZ, COUNT(I2C_HZ), m->i2c_hz);
        if (hb >= 0) snprintf(label, sizeof(label), "HB\n%d", HB_MS[hb]);
        else snprintf(label, sizeof(label), "HB\n%d?", m->hb_ms);
        set_btn(&b[0], label, "", C_ACC, BA_HB, 0);
        snprintf(label, sizeof(label), "I2C %s", i2c >= 0 ? I2C_NAMES[i2c] : "??");
        set_btn(&b[1], label, "", C_ACC, BA_I2C, 0);
        return 2;
    }
    case 1: // Drive / roll-yaw
        set_btn(&b[0], "R-FW", "b45,-45", C_ACC, BA_SEND, 0);
        set_btn(&b[1], "R-BW", "b-45,45", C_ACC, BA_SEND, 0);
        set_btn(&b[2], "Y-CW", "b45,45", C_ACC2, BA_SEND, 0);
        set_btn(&b[3], "Y-CCW", "b-45,-45", C_ACC2, BA_SEND, 0);
        return 4;
    case 2: // Drive / jog
        set_btn(&b[0], "X+50", "b50,0", C_CYAN, BA_SEND, 0);
        set_btn(&b[1], "X-50", "b-50,0", C_CYAN, BA_SEND, 0);
        set_btn(&b[2], "Y+50", "b0,50", C_CYAN, BA_SEND, 0);
        set_btn(&b[3], "Y-50", "b0,-50", C_CYAN, BA_SEND, 0);
        return 4;
    case 3: // Drive / Z rail
        if (!HMI_RAIL_PID) set_btn(&b[0], "PID\nlocked", "", C_GREY, BA_NONE, 0);
        else set_btn(&b[0], m->pid_mode ? "PID" : "Open\nLoop", "", m->pid_mode ? C_ON : C_ACC2, BA_PID, 0);
        if (!HMI_RAIL_PID && m->pid_mode) {
            set_btn(&b[1], "TGT\nlocked", "", C_GREY, BA_NONE, 0);
            set_btn(&b[2], "TGT\nlocked", "", C_GREY, BA_NONE, 0);
        } else if (m->pid_mode) {
            snprintf(label, sizeof(label), "TGT\n+%d", PID_STEP);
            set_btn(&b[1], label, "", C_ACC, BA_TARGET, PID_STEP);
            snprintf(label, sizeof(label), "TGT\n-%d", PID_STEP);
            set_btn(&b[2], label, "", C_ACC, BA_TARGET, -PID_STEP);
        } else if (HMI_Z_STEP_BUTTONS) {
            snprintf(label, sizeof(label), "STP\n+%d", Z_STEP_DELTA);
            set_btn(&b[1], label, "", C_ACC, BA_ZSTEP, Z_STEP_DELTA);
            snprintf(label, sizeof(label), "STP\n-%d", Z_STEP_DELTA);
            set_btn(&b[2], label, "", C_ACC, BA_ZSTEP, -Z_STEP_DELTA);
        } else {
            set_btn(&b[1], "STP\nlocked", "", C_GREY, BA_NONE, 0);
            set_btn(&b[2], "STP\nlocked", "", C_GREY, BA_NONE, 0);
        }
        set_btn(&b[3], "HOME", "home", C_OFF, BA_SEND, 0);
        return 4;
    case 4: // Robot / network. The old HB button sent "mzh" here, which Robot3AxisMCU ignores.
        set_btn(&b[0], "REFRESH", "refresh", C_ACC, BA_REFRESH, 0);
        return 1;
    case 5: { // Robot / axis jog
        static const char axis[3] = {'X', 'Y', 'Z'};
        for (int i = 0; i < 3; ++i) {
            snprintf(label, sizeof(label), "%c+50", axis[i]);
            snprintf(cmd, sizeof(cmd), "r%c50", axis[i] + 32);
            set_btn(&b[i * 2], label, cmd, C_CYAN, BA_SEND, 0);
            snprintf(label, sizeof(label), "%c-50", axis[i]);
            snprintf(cmd, sizeof(cmd), "r%c-50", axis[i] + 32);
            set_btn(&b[i * 2 + 1], label, cmd, C_CYAN, BA_SEND, 0);
        }
        return 6;
    }
    case 6: // Robot / gripper
        set_btn(&b[0], "GRAB\nOPEN", "grab open", C_ON, BA_SEND, 0);
        set_btn(&b[1], "GRAB\nCLOSE", "grab close", C_OFF, BA_SEND, 0);
        set_btn(&b[2], "HOME", "home", C_ACC, BA_SEND, 0);
        return 3;
    case 7: { // Robot / max speed
        static const char axis[3] = {'X', 'Y', 'Z'};
        static const int step[3] = {10, 50, 50};
        for (int i = 0; i < 3; ++i) {
            snprintf(label, sizeof(label), "%c SPD+", axis[i]);
            snprintf(cmd, sizeof(cmd), "r%cs%d", axis[i] + 32, step[i]);
            set_btn(&b[i * 2], label, cmd, C_ACC, BA_SPEED, i * 2);
            snprintf(label, sizeof(label), "%c SPD-", axis[i]);
            snprintf(cmd, sizeof(cmd), "r%cs-%d", axis[i] + 32, step[i]);
            set_btn(&b[i * 2 + 1], label, cmd, C_ACC, BA_SPEED, i * 2 + 1);
        }
        return 6;
    }
    case 8: // Cam / network
        set_btn(&b[0], "SPI 10M", "cspi10", C_ACC2, BA_SEND, 0);
        set_btn(&b[1], "SPI 20M", "cspi20", C_ACC2, BA_SEND, 0);
        return 2;
    case 9: // Cam / capture
        set_btn(&b[0], "SHOT", "capture", C_ACC, BA_SEND, 0);
        set_btn(&b[1], ui.cam_view ? "HIDE" : "VIEW", "", C_ON, BA_VIEW, 0);
        set_btn(&b[2], "AI\nANALYZE", "analyze", C_ACC2, BA_SEND, 0);
        set_btn(&b[3], "STREAM", "stream", ui.cam_view ? C_OFF : C_BTN, BA_SEND, 0);
        return 4;
    case 10: { // Cam / config
        int r = find_str(CAM_RES, COUNT(CAM_RES), m->cam_res);
        snprintf(label, sizeof(label), "SENS\n%s", m->cam_sensor);
        set_btn(&b[0], label, "c_sens", C_ACC, BA_CAM_SENS, 0);
        snprintf(label, sizeof(label), "%s\n%s", m->cam_res, r >= 0 ? CAM_RES_PX[r] : "");
        set_btn(&b[1], label, "c_res", C_ACC, BA_CAM_RES, 0);
        snprintf(label, sizeof(label), "QUAL\n%d", m->cam_quality);
        set_btn(&b[2], label, "c_qlt", C_ACC, BA_CAM_QUAL, 0);
        return 3;
    }
    case 11: // Cam / AI model
        set_btn(&b[0], "AI MDL\nNEXT", "c_mdl", C_ACC, BA_REFRESH, 0);
        return 1;
    case 12: // Ctrl (ControlMCU voice)
        set_btn(&b[0], "VAD", "vad", C_ON, BA_SEND, 0);
        set_btn(&b[1], "PTT", "ptt", C_ACC, BA_SEND, 0);
        set_btn(&b[2], "MUTE", "mute", C_OFF, BA_SEND, 0);
        set_btn(&b[3], "PLAY", "play", C_ON, BA_SEND, 0);
        return 4;
    }
    return 0;
}

// ---- Drawing helpers ------------------------------------------------------------
static void draw_signal(int x, int y, int rssi, bool ok) {
    int bars = !ok ? 0 : rssi > -70 ? 3 : rssi > -80 ? 2 : rssi > -90 ? 1 : 0;
    for (int i = 0; i < 3; ++i) {
        int bh = 5 + i * 3;
        gfx_fill_rect(x + i * 6, y + (11 - bh), 4, bh, i < bars ? C_ON : C_SEP);
    }
}

// Dark buttons get white labels; the Arduino build drew near-black text on dark blue.
static uint16_t label_color(uint16_t bg) {
    int r = (bg >> 11) << 3, g = ((bg >> 5) & 63) << 2, b = (bg & 31) << 3;
    return (299 * r + 587 * g + 114 * b) / 1000 < 140 ? C_BG : C_TEXT;
}

static void draw_dot(int x, int y, bool on) { gfx_fill_circle(x, y, 4, on ? C_ON : C_OFF); }

static void kv(int y, const char *key, const char *value, uint16_t color) {
    gfx_text(8, y, key, C_DIM, 1, GFX_TL);
    gfx_text(8 + gfx_text_width(key, 1) + 8, y, value, color, 1, GFX_TL);
}

// Clips a UTF-8 value to the info panel width.
static void kv_utf8(int y, const char *key, const char *value, uint16_t color) {
    char fit[64];
    int x = 8 + gfx_text_width(key, 1) + 8;
    gfx_text(8, y, key, C_DIM, 1, GFX_TL);
    gfx_utf8_fit(value, INFO_W - x - 4, fit, sizeof(fit));
    gfx_text_utf8(x, y + 4, fit, color, GFX_ML);
}

static void draw_header(void) {
    const hmi_model_t *m = &ui.m;
    gfx_fill_rect(0, 0, HMI_W, HDR_H, C_HDR);
    gfx_hline(0, HDR_H, HMI_W, C_SEP);
    gfx_text(6, 6, ui.drawn_clock, C_TEXT, 2, GFX_TL);
    gfx_text(HMI_W / 2, HDR_H / 2, "ARD", C_ACC, 2, GFX_MC);
    draw_signal(HMI_W - 32, 6, m->wifi_rssi, m->wifi_ok);
    draw_dot(HMI_W - 8, 14, m->wifi_ok && m->server_ok);
}

static void draw_tabs(void) {
    int tw = HMI_W / TAB_COUNT;
    for (int i = 0; i < TAB_COUNT; ++i) {
        int tx = i * tw;
        bool active = i == ui.tab;
        uint16_t bg = active ? C_TABA : C_HDR;
        gfx_fill_rect(tx, HDR_H, tw, TAB_H, bg);
        if (active) gfx_fill_rect(tx, HDR_H + TAB_H - 3, tw, 3, C_ACC);
        gfx_text(tx + tw / 2, HDR_H + TAB_H / 2, TAB_NAMES[i], active ? C_TEXT : C_DIM, 1, GFX_MC);
        // the online dot doubles as a per-board status light
        gfx_fill_circle(tx + tw - 7, HDR_H + 7, 2, ui.m.dev[i].online ? C_ON : C_SEP);
        if (i < TAB_COUNT - 1) gfx_vline(tx + tw, HDR_H, TAB_H, C_SEP);
    }
    gfx_hline(0, HDR_H + TAB_H, HMI_W, C_SEP);
}

static void draw_subbar(void) {
    int y = HDR_H + TAB_H;
    char title[32];
    gfx_fill_rect(0, y, HMI_W, SUB_H, C_BTN);
    gfx_hline(0, y + SUB_H - 1, HMI_W, C_SEP);
    snprintf(title, sizeof(title), "[ %s ]", PAGE_TITLES[ui.tab][ui.page[ui.tab]]);
    gfx_text(HMI_W / 2, y + SUB_H / 2, title, C_DIM, 1, GFX_MC);
}

static void info_network(int tab, int y) {
    const hmi_device_t *d = &ui.m.dev[tab];
    char buf[24];
    kv(y + 36, "IP:", d->ip[0] ? d->ip : "---", C_TEXT);
    snprintf(buf, sizeof(buf), "%ddBm", d->rssi);
    kv(y + 56, "RSSI:", d->online ? buf : "---", C_TEXT);
    snprintf(buf, sizeof(buf), "%.1fC", d->temp);
    kv(y + 76, "Temp:", d->online ? buf : "---", C_TEXT);
    kv(y + 96, "Seen:", d->last_seen, C_ACC);
}

static void info_drive(int page, int y) {
    const hmi_model_t *m = &ui.m;
    char buf[24];
    if (page == 0) {
        info_network(DEV_DRIVE, y);
        snprintf(buf, sizeof(buf), "%dkHz", m->i2c_hz / 1000);
        kv(y + 116, "I2C:", buf, C_TEXT);
        snprintf(buf, sizeof(buf), "%dms", m->hb_ms);
        kv(y + 136, "HB:", buf, C_TEXT);
    } else if (page == 1 || page == 2) {
        snprintf(buf, sizeof(buf), "%dmm", m->tof_mm);
        kv(y + 36, "ToF:", m->has_tof ? buf : "---", C_TEXT);
        gfx_text(8, y + 66, page == 1 ? "Roll FW/BW, Yaw CW/CCW" : "Jog disc motors", C_DIM, 1, GFX_TL);
        gfx_text(8, y + 82, page == 1 ? "45 steps per press" : "50 steps per press", C_DIM, 1, GFX_TL);
    } else {
        gfx_text(8, y + 26, "Z-Control |", C_TEXT, 1, GFX_TL);
        gfx_text(100, y + 26, m->pid_mode ? "PID" : "OPEN", m->pid_mode ? C_ON : C_OFF, 1, GFX_TL);
        // Rail gauge: 145..175 mm mapped onto the bar, ToF marker left, target right.
        const int rx = 140, ry = y + 30, rh = 110;
        gfx_fill_rect(rx, ry, 6, rh, C_SEP);
        int tof = m->has_tof ? m->tof_mm : PID_MIN;
        int ty = ry + rh - (tof - PID_MIN) * rh / (PID_MAX - PID_MIN);
        ty = ty < ry ? ry : ty > ry + rh ? ry + rh : ty;
        if (m->has_tof) gfx_fill_rect(rx - 10, ty - 1, 10, 3, C_ON);
        if (m->pid_mode) {
            int gy = ry + rh - (m->pid_target - PID_MIN) * rh / (PID_MAX - PID_MIN);
            gy = gy < ry ? ry : gy > ry + rh ? ry + rh : gy;
            gfx_fill_rect(rx + 6, gy - 1, 10, 3, C_ACC2);
        }
        snprintf(buf, sizeof(buf), "%dmm", m->tof_mm);
        kv(y + 50, "ToF:", m->has_tof ? buf : "---", C_ON);
        if (m->pid_mode) {
            snprintf(buf, sizeof(buf), "%dmm", m->pid_target);
            kv(y + 72, "TGT:", buf, C_ACC2);
        } else {
            snprintf(buf, sizeof(buf), "%d", m->z_step);
            kv(y + 72, "STP:", buf, C_TEXT);
        }
    }
}

static void info_robot(int page, int y) {
    const hmi_model_t *m = &ui.m;
    char buf[24];
    if (page == 0) {
        info_network(DEV_ROBOT, y);
    } else if (page == 1) {
        snprintf(buf, sizeof(buf), "%.1f deg", m->robot_x_deg);
        kv(y + 36, "X:", buf, C_TEXT);
        snprintf(buf, sizeof(buf), "%.1f mm", m->robot_y_mm);
        kv(y + 56, "Y:", buf, C_TEXT);
        snprintf(buf, sizeof(buf), "%.1f mm", m->robot_z_mm);
        kv(y + 76, "Z:", buf, C_TEXT);
        gfx_text(8, y + 110, "Control: Axis Pos", C_ACC, 1, GFX_TL);
    } else if (page == 2) {
        kv(y + 36, "Gripper:", m->grab, !strcmp(m->grab, "open") ? C_ON : C_OFF);
        kv(y + 60, "Valve:", "Dual-Solenoid", C_TEXT);
        gfx_text(8, y + 110, "Control: Pneumatic", C_ACC, 1, GFX_TL);
    } else {
        static const char *keys[3] = {"X Spd:", "Y Spd:", "Z Spd:"};
        for (int i = 0; i < 3; ++i) {
            snprintf(buf, sizeof(buf), "%d s/s", m->robot_speed[i]);
            kv(y + 36 + i * 20, keys[i], buf, C_TEXT);
        }
        gfx_text(8, y + 110, "Control: Max Speed", C_ACC, 1, GFX_TL);
    }
}

static void info_cam_preview(int y) {
    uint32_t seq;
    const char *error = NULL;
    const gfx_image_t *img = hmi_image_acquire(&seq, &error);
    if (img) {
        gfx_image((INFO_W - img->w) / 2, y + (CONT_H - img->h) / 2, img);
    } else {
        gfx_text(INFO_W / 2, y + CONT_H / 2 - 8, error ? error : "Fetching image...", error ? C_OFF : C_DIM, 1,
                 GFX_MC);
    }
    hmi_image_release();
}

static void info_cam(int page, int y) {
    const hmi_model_t *m = &ui.m;
    char buf[24];
    if (page == 0) {
        info_network(DEV_CAM, y);
        gfx_text(8, y + 130, "CAM System Ready", m->dev[DEV_CAM].online ? C_ON : C_DIM, 1, GFX_TL);
    } else if (page == 1) {
        if (ui.cam_view) {
            info_cam_preview(y);
            return;
        }
        gfx_text(8, y + 36, "AI Result:", C_DIM, 1, GFX_TL);
        kv_utf8(y + 58, "OBJ:", m->ai_object, C_ACC);
        kv_utf8(y + 82, "CAT:", m->ai_category, C_ACC);
        kv(y + 145, "Last Update:", m->dev[DEV_CAM].last_seen, C_DIM);
    } else if (page == 2) {
        kv(y + 36, "Sensor:", m->cam_sensor, C_TEXT);
        kv(y + 56, "Res:", m->cam_res, C_TEXT);
        snprintf(buf, sizeof(buf), "%d", m->cam_quality);
        kv(y + 76, "Quality:", buf, C_TEXT);
        gfx_text(8, y + 110, "Control: Cam Config", C_ACC, 1, GFX_TL);
    } else {
        gfx_text(8, y + 36, "AI Model:", C_DIM, 1, GFX_TL);
        char fit[48];
        gfx_utf8_fit(m->ai_model, INFO_W - 16, fit, sizeof(fit));
        gfx_text_utf8(8, y + 60, fit, C_ACC, GFX_ML);
        gfx_text(8, y + 110, "NEXT cycles the model", C_DIM, 1, GFX_TL);
    }
}

static void info_sound(int y) {
    const hmi_model_t *m = &ui.m;
    char buf[24];
    kv(y + 36, "IP:", m->dev[DEV_SOUND].ip[0] ? m->dev[DEV_SOUND].ip : "---", C_TEXT);
    snprintf(buf, sizeof(buf), "%ddBm", m->dev[DEV_SOUND].rssi);
    kv(y + 56, "RSSI:", m->dev[DEV_SOUND].online ? buf : "---", C_TEXT);
    kv(y + 76, "Mode:", m->sound_mode, C_ACC);
    kv_utf8(y + 96, "State:", m->sound_status, C_TEXT);
    gfx_hline(6, y + 116, INFO_W - 12, C_SEP);
    kv(y + 128, "Server:", m->server[0] ? m->server : "---", C_ACC);
    draw_signal(8, y + 145, m->wifi_rssi, m->wifi_ok);
}

static void draw_info(void) {
    int y = CONT_Y;
    gfx_fill_rect(0, y, INFO_W, CONT_H, C_INFO);
    gfx_vline(INFO_W, y, CONT_H, C_SEP);
    int page = ui.page[ui.tab];
    bool preview = ui.tab == DEV_CAM && page == 1 && ui.cam_view;
    if (!preview) {
        gfx_text(8, y + 10, DEV_NAMES[ui.tab], C_TEXT, 1, GFX_TL);
        draw_dot(INFO_W - 12, y + 12, ui.m.dev[ui.tab].online);
    }
    switch (ui.tab) {
    case 0: info_drive(page, y); break;
    case 1: info_robot(page, y); break;
    case 2: info_cam(page, y); break;
    default: info_sound(y); break;
    }
}

static void draw_buttons(void) {
    btn_t b[MAX_BTNS];
    int n = build_buttons(&ui.m, ui.tab, ui.page[ui.tab], b);
    gfx_fill_rect(INFO_W + 1, CONT_Y, RBTN_W - 1, CONT_H, C_BG);
    if (!n) return;
    int bh = CONT_H / n;
    for (int i = 0; i < n; ++i) {
        int by = CONT_Y + i * bh;
        bool pressed = ui.press == PRESS_BTN && ui.press_index == i;
        uint16_t col = pressed ? C_ON : b[i].color;
        gfx_fill_round_rect(INFO_W + 3, by + 3, RBTN_W - 6, bh - 6, 5, col);
        gfx_round_rect(INFO_W + 2, by + 2, RBTN_W - 4, bh - 4, 6, C_SEP);
        gfx_text(INFO_W + RBTN_W / 2, by + bh / 2, b[i].label, b[i].action == BA_NONE ? C_DIM : label_color(col), 1, GFX_MC);
    }
}

static void draw_bottom(void) {
    const hmi_model_t *m = &ui.m;
    int y = BOT_Y;
    char buf[32];
    gfx_fill_rect(0, y, HMI_W, BOT_H, C_PANEL);
    gfx_hline(0, y, HMI_W, C_SEP);
    snprintf(buf, sizeof(buf), "PAGE %d/%d", ui.page[ui.tab] + 1, PAGE_COUNT[ui.tab]);
    gfx_text(8, y + 25, buf, C_TEXT, 1, GFX_TL);
    gfx_text(8, y + 45, TAB_NAMES[ui.tab], C_ACC, 1, GFX_TL);
    const char *mode = "Offline";
    if (!strcmp(m->ctrl_mode, "A")) mode = "A(Robot X-Z)";
    else if (!strcmp(m->ctrl_mode, "N")) mode = "N(Robot Y)";
    else if (!strcmp(m->ctrl_mode, "B")) mode = "B(Drive)";
    snprintf(buf, sizeof(buf), "CTRL: %s", mode);
    gfx_text(8, y + 65, buf, C_AMBER, 1, GFX_TL);

    bool can_prev = ui.page[ui.tab] > 0, can_next = ui.page[ui.tab] < PAGE_COUNT[ui.tab] - 1;
    uint16_t lc = ui.press == PRESS_PREV ? C_ON : can_prev ? C_ACC : C_BTN;
    uint16_t rc = ui.press == PRESS_NEXT ? C_ON : can_next ? C_ACC : C_BTN;
    gfx_fill_round_rect(NAV_X, y + 16, NAV_W - 4, 44, 4, lc);
    gfx_round_rect(NAV_X, y + 16, NAV_W - 4, 44, 4, C_SEP);
    gfx_text(NAV_X + (NAV_W - 4) / 2, y + 38, "<", label_color(lc), 2, GFX_MC);
    gfx_fill_round_rect(NAV_X + NAV_W, y + 16, NAV_W - 4, 44, 4, rc);
    gfx_round_rect(NAV_X + NAV_W, y + 16, NAV_W - 4, 44, 4, C_SEP);
    gfx_text(NAV_X + NAV_W + (NAV_W - 4) / 2, y + 38, ">", label_color(rc), 2, GFX_MC);
}

static bool approval_visible(void) {
    if (!ui.m.approval) return false;
    // After answering, hide the popup until the server drops the action. If the
    // answer never arrived, the action shows up again after 10 s.
    return strcmp(ui.m.approval_id, ui.answered_id) != 0 || hmi_millis() - ui.answered_at > 10000;
}

static void draw_approval(void) {
    gfx_fill_rect(10, 60, 220, 200, C_HDR);
    gfx_rect(10, 60, 220, 200, C_ACC);
    gfx_text(120, 80, "AI Command", C_TEXT, 2, GFX_MC);
    // Wrap the (often Korean) description over up to three lines.
    const char *p = ui.m.approval_desc;
    int y = 108;
    for (int line = 0; line < 3 && *p; ++line, y += 18) {
        char fit[64];
        int used = gfx_utf8_fit(p, 200, fit, sizeof(fit));
        if (!used) break;
        gfx_text_utf8(120, y, fit, C_ACC, GFX_MC);
        p += used;
    }
    gfx_text(120, 165, "Approve?", C_TEXT, 2, GFX_MC);
    gfx_fill_round_rect(20, 180, 80, 60, 5, ui.press == PRESS_OK ? C_ACC : C_ON);
    gfx_text(60, 210, "OK", C_BG, 2, GFX_MC);
    gfx_fill_round_rect(140, 180, 80, 60, 5, ui.press == PRESS_NO ? C_ACC : C_OFF);
    gfx_text(180, 210, "NO", C_BG, 2, GFX_MC);
}

static void scene_main(void *ctx) {
    gfx_fill_rect(0, 0, HMI_W, HMI_H, C_BG);
    draw_header();
    if (approval_visible()) {
        draw_approval();
        return;
    }
    draw_tabs();
    draw_subbar();
    draw_info();
    draw_buttons();
    draw_bottom();
}

static void scene_splash(void *ctx) {
    gfx_fill_rect(0, 0, HMI_W, HMI_H, C_BG);
    if (ui.logo) gfx_image((HMI_W - ui.logo->w) / 2, 184 - ui.logo->h - 8, ui.logo);
    // 19 chars at scale 2 (304 px) ran off the 240 px screen in the Arduino build.
    gfx_text(HMI_W / 2, 201, "ARD HMI", C_ACC, 2, GFX_MC);
    gfx_text(HMI_W / 2, 224, "System v3.0 | ESP-IDF", C_DIM, 1, GFX_MC);
    gfx_hline(20, 241, HMI_W - 40, C_SEP);
    gfx_text(HMI_W / 2, 258, ui.splash_status ? ui.splash_status : "", C_TEXT, 1, GFX_MC);
}

// ---- Public -----------------------------------------------------------------------
void hmi_ui_init(const gfx_target_t *target) {
    memset(&ui, 0, sizeof(ui));
    ui.target = *target;
    hmi_model_defaults(&ui.m);
    strcpy(ui.drawn_clock, "--:--");
}

void hmi_ui_splash(const gfx_image_t *logo, const char *status) {
    ui.logo = logo;
    ui.splash_status = status;
    gfx_render(&ui.target, 0, HMI_H, scene_splash, NULL);
}

void hmi_ui_start(void) {
    ui.started = true;
    ui.logo = NULL;
    ui.dirty = true;
    hmi_ui_tick();
}

static void redraw(int y0, int y1) { gfx_render(&ui.target, y0, y1, scene_main, NULL); }

static void flash(press_t what, int index) {
    ui.press = what;
    ui.press_index = index;
    ui.press_until = hmi_millis() + 120;
    ui.dirty = true;
}

// Optimistic model edits (run under the platform lock).
typedef struct { btn_action_t action; int arg; int value; const char *text; } edit_t;

static void apply_edit(hmi_model_t *m, void *arg) {
    const edit_t *e = arg;
    switch (e->action) {
    case BA_HB: m->hb_ms = e->value; break;
    case BA_I2C: m->i2c_hz = e->value; break;
    case BA_PID: m->pid_mode = e->value; break;
    case BA_TARGET: m->pid_target = e->value; break;
    case BA_ZSTEP: m->z_step = e->value; break;
    case BA_SPEED: m->robot_speed[e->arg / 2] = e->value; break;
    case BA_CAM_SENS: snprintf(m->cam_sensor, sizeof(m->cam_sensor), "%s", e->text); break;
    case BA_CAM_RES: snprintf(m->cam_res, sizeof(m->cam_res), "%s", e->text); break;
    case BA_CAM_QUAL: m->cam_quality = e->value; break;
    default: break;
    }
}

static void edit(btn_action_t action, int arg, int value, const char *text) {
    edit_t e = {action, arg, value, text};
    hmi_model_edit(apply_edit, &e);
    hmi_model_get(&ui.m);
}

static void press_button(const btn_t *b) {
    const hmi_model_t *m = &ui.m;
    const hmi_target_t target = (hmi_target_t)ui.tab;
    char cmd[24];
    int i;
    switch (b->action) {
    case BA_SEND:
        hmi_send_command(target, b->cmd);
        break;
    case BA_REFRESH:
        hmi_send_command(target, b->cmd);
        hmi_request_refresh();
        break;
    case BA_HB:
        i = (find_int(HB_MS, COUNT(HB_MS), m->hb_ms) + 1) % COUNT(HB_MS);
        snprintf(cmd, sizeof(cmd), "mzh%d", HB_MS[i]);
        edit(BA_HB, 0, HB_MS[i], NULL);
        hmi_send_command(target, cmd);
        break;
    case BA_I2C:
        i = (find_int(I2C_HZ, COUNT(I2C_HZ), m->i2c_hz) + 1) % COUNT(I2C_HZ);
        snprintf(cmd, sizeof(cmd), "mzc%d", I2C_HZ[i]);
        edit(BA_I2C, 0, I2C_HZ[i], NULL);
        hmi_send_command(target, cmd);
        break;
    case BA_PID:
        edit(BA_PID, 0, !m->pid_mode, NULL);
        hmi_send_command(target, ui.m.pid_mode ? "mzm1" : "mzm0");
        break;
    case BA_TARGET: {
        int v = m->pid_target + b->arg;
        v = v < PID_MIN ? PID_MIN : v > PID_MAX ? PID_MAX : v;
        snprintf(cmd, sizeof(cmd), "mzt%d", v);
        edit(BA_TARGET, 0, v, NULL);
        hmi_send_command(target, cmd);
        break;
    }
    case BA_ZSTEP: {
        // The server keeps z_step and adds the signed delta; send the delta, not the total.
        int v = m->z_step + b->arg;
        snprintf(cmd, sizeof(cmd), "mzs%+d", b->arg);
        edit(BA_ZSTEP, 0, v < 10 ? 10 : v, NULL);
        hmi_send_command(target, cmd);
        break;
    }
    case BA_SPEED: {
        static const int step[3] = {10, 50, 50};
        int axis = b->arg / 2;
        int v = m->robot_speed[axis] + (b->arg & 1 ? -step[axis] : step[axis]);
        edit(BA_SPEED, b->arg, v < 100 ? 100 : v, NULL);
        hmi_send_command(target, b->cmd);
        break;
    }
    case BA_VIEW:
        ui.cam_view = !ui.cam_view;
        hmi_image_want(ui.cam_view);
        break;
    case BA_CAM_SENS:
        i = find_str(CAM_SENSORS, COUNT(CAM_SENSORS), m->cam_sensor);
        edit(BA_CAM_SENS, 0, 0, CAM_SENSORS[(i + 1) % COUNT(CAM_SENSORS)]);
        hmi_send_command(target, b->cmd);
        break;
    case BA_CAM_RES:
        i = find_str(CAM_RES, COUNT(CAM_RES), m->cam_res);
        edit(BA_CAM_RES, 0, 0, CAM_RES[(i + 1) % COUNT(CAM_RES)]);
        hmi_send_command(target, b->cmd);
        break;
    case BA_CAM_QUAL:
        i = find_int(CAM_QUALITY, COUNT(CAM_QUALITY), m->cam_quality);
        edit(BA_CAM_QUAL, 0, CAM_QUALITY[(i + 1) % COUNT(CAM_QUALITY)], NULL);
        hmi_send_command(target, b->cmd);
        break;
    case BA_NONE:
        break;
    }
}

static void set_page(int tab, int page) {
    if (ui.tab == DEV_CAM && ui.cam_view && (tab != DEV_CAM || page != 1)) {
        // Leaving the preview stops the periodic image download.
        ui.cam_view = false;
        hmi_image_want(false);
    }
    ui.tab = tab;
    ui.page[tab] = page;
    ui.dirty = true;
}

void hmi_ui_touch(int x, int y) {
    if (!ui.started) return;
    if (approval_visible()) {
        if (y < 180 || y > 240) return;
        bool ok = x >= 20 && x <= 100, no = x >= 140 && x <= 220;
        if (!ok && !no) return;
        hmi_send_approval(ui.m.approval_id, ok);
        snprintf(ui.answered_id, sizeof(ui.answered_id), "%s", ui.m.approval_id);
        ui.answered_at = hmi_millis();
        ui.dirty = true;
        return;
    }
    if (y < HDR_H) return;
    if (y < HDR_H + TAB_H) {
        int tab = x / (HMI_W / TAB_COUNT);
        if (tab >= 0 && tab < TAB_COUNT && tab != ui.tab) set_page(tab, 0);
        return;
    }
    if (y < CONT_Y) return;
    if (y < BOT_Y) {
        if (x < INFO_W) return;
        btn_t b[MAX_BTNS];
        int n = build_buttons(&ui.m, ui.tab, ui.page[ui.tab], b);
        if (!n) return;
        int i = (y - CONT_Y) / (CONT_H / n);
        if (i < 0 || i >= n || b[i].action == BA_NONE) return;
        flash(PRESS_BTN, i);
        press_button(&b[i]);
        return;
    }
    if (y < BOT_Y + 10 || y > BOT_Y + 66) return;
    int page = ui.page[ui.tab];
    if (x >= NAV_X && x < NAV_X + NAV_W - 4) {
        flash(PRESS_PREV, 0);
        if (page > 0) set_page(ui.tab, page - 1);
    } else if (x >= NAV_X + NAV_W && x <= HMI_W - 4) {
        flash(PRESS_NEXT, 0);
        if (page < PAGE_COUNT[ui.tab] - 1) set_page(ui.tab, page + 1);
    }
}

void hmi_ui_tick(void) {
    if (!ui.started) return;
    uint32_t now = hmi_millis();
    if (ui.press != PRESS_NONE && (int32_t)(now - ui.press_until) >= 0) {
        ui.press = PRESS_NONE;
        ui.dirty = true;
    }
    hmi_model_get(&ui.m);
    if (ui.m.revision != ui.drawn_revision) ui.dirty = true;
    // An answered popup comes back after the timeout if the server still lists it.
    static bool popup_was_visible;
    bool popup = approval_visible();
    if (popup != popup_was_visible) ui.dirty = true;
    popup_was_visible = popup;

    char clock[8];
    if (!hmi_clock(clock, sizeof(clock))) strcpy(clock, "--:--");
    bool clock_changed = strcmp(clock, ui.drawn_clock) != 0;
    strcpy(ui.drawn_clock, clock);

    bool image_changed = false;
    if (ui.cam_view) {
        uint32_t seq;
        const char *error;
        hmi_image_acquire(&seq, &error);
        hmi_image_release();
        image_changed = seq != ui.drawn_image_seq;
        ui.drawn_image_seq = seq;
    }

    if (ui.dirty) {
        ui.dirty = false;
        ui.drawn_revision = ui.m.revision;
        redraw(0, HMI_H);
    } else if (image_changed) {
        redraw(CONT_Y, CONT_Y + CONT_H);
    } else if (clock_changed) {
        redraw(0, HDR_H + 1);
    }
}
