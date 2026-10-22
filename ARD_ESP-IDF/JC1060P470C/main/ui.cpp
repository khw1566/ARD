#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ard_network.h"
#include "ard_setup_qr.h"
#include "board.h"
#include "bsp_display.h"
#include "commands.h"
#include "controls.h"
#include "gamepad.h"
#include "gps.h"
#include "motion.h"
#include "pc_link.h"
#include "sensors.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"

namespace {

// Light theme close to the old HMI palette, with stronger contrast for a 7" panel.
const lv_color_t kBar = lv_color_hex(0x1F2A36), kAccent = lv_color_hex(0x0B6E99), kOn = lv_color_hex(0x1E9E50),
                 kOff = lv_color_hex(0xB3261E), kMuted = lv_color_hex(0x5F6B76), kPanel = lv_color_hex(0xF2F4F6);

enum Tab { TAB_DRIVE, TAB_ROBOT, TAB_CAM, TAB_SOUND, TAB_SYSTEM, TAB_COUNT };

struct Ui {
    lv_obj_t *tabview;
    lv_obj_t *clock, *net, *pc, *pad, *armed;
    // Drive
    lv_obj_t *pos, *driveState, *env, *tof, *gps, *jogSpeed, *jogSpeedLabel, *pidButton, *railLabel, *stepLabel;
    // Robot / Cam / Sound
    lv_obj_t *robotOnline, *robotInfo, *camOnline, *camInfo, *camImage, *camImageNote, *soundOnline, *soundInfo;
    // System
    lv_obj_t *netInfo, *padInfo, *padRaw, *sysInfo, *brightness;
    // Approval popup
    lv_obj_t *approval, *approvalText;
    char approvalId[40];
};

Ui ui;
int railTarget = 145, zStep = 50;   // same defaults and ranges as HMI_MCU page 4
bool pidOn = false;
uint8_t *imageCopy;
size_t imageCopySize;
lv_image_dsc_t imageDsc;
uint32_t imageSeq = 0;
volatile int pendingTabStep = 0;

// ---- helpers ---------------------------------------------------------------------------------

lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font = &lv_font_montserrat_20,
                lv_color_t color = lv_color_black()) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, lv_color_t color, lv_event_cb_t cb, void *data,
                 int w = 150, int h = 64) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, color, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *l = label(b, text, &lv_font_montserrat_20, lv_color_white());
    lv_obj_center(l);
    return b;
}

lv_obj_t *panel(lv_obj_t *parent, int w, int h) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, kPanel, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 12, 0);
    lv_obj_set_style_pad_all(p, 14, 0);
    return p;
}

lv_obj_t *grid(lv_obj_t *parent, int w, int h) {
    lv_obj_t *g = lv_obj_create(parent);
    lv_obj_set_size(g, w, h);
    lv_obj_set_style_border_width(g, 0, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g, 4, 0);
    lv_obj_set_style_pad_gap(g, 10, 0);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
    return g;
}

void localCommand(lv_event_t *e) {
    const char *cmd = static_cast<const char *>(lv_event_get_user_data(e));
    command_execute(cmd); // DriveMCU commands run here directly, no PC round trip
}

void robotCommand(lv_event_t *e) { pc_send_command(PC_ROBOT, static_cast<const char *>(lv_event_get_user_data(e))); }
void camCommand(lv_event_t *e) { pc_send_command(PC_CAM, static_cast<const char *>(lv_event_get_user_data(e))); }
void soundCommand(lv_event_t *e) { pc_send_command(PC_SOUND, static_cast<const char *>(lv_event_get_user_data(e))); }

void estop(lv_event_t *) { command_execute("stop"); }
void rearm(lv_event_t *) { motion_clear_stop(); }

// ---- Drive tab ---------------------------------------------------------------------------------

void updateRailLabels() {
    char text[48];
    snprintf(text, sizeof(text), pidOn ? "PID target: %d mm" : "Z step: %d steps", pidOn ? railTarget : zStep);
    lv_label_set_text(ui.railLabel, text);
    if (ui.pidButton) lv_label_set_text(lv_obj_get_child(ui.pidButton, 0), pidOn ? "PID: ON" : "PID: OFF");
}

void pidToggle(lv_event_t *) {
    const bool want = !pidOn;
    if (command_execute(want ? "mzm1" : "mzm0")) pidOn = want;
    updateRailLabels();
}

void railAdjust(lv_event_t *e) {
    const int dir = (int)(intptr_t)lv_event_get_user_data(e);
    char cmd[24];
    if (pidOn) {
        railTarget += dir * 5;
        if (railTarget < 145) railTarget = 145;
        if (railTarget > 175) railTarget = 175;
        snprintf(cmd, sizeof(cmd), "mzt%d", railTarget);
    } else {
        // HMI_MCU sent "mzs+50" here, which overwrote the Steps/mm calibration.
        // The intent was moving the rail by the step size, so this sends a Z move.
        snprintf(cmd, sizeof(cmd), "mzr%d", dir * zStep);
    }
    command_execute(cmd);
    updateRailLabels();
}

void zStepChange(lv_event_t *e) {
    zStep += (int)(intptr_t)lv_event_get_user_data(e) * 10;
    if (zStep < 10) zStep = 10;
    if (zStep > 2000) zStep = 2000;
    updateRailLabels();
}

void jogSpeedChanged(lv_event_t *) { controls_set_speed(lv_slider_get_value(ui.jogSpeed)); }

void buildDrive(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(tab, 12, 0);
    lv_obj_t *left = panel(tab, 360, 470);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(left, 10, 0);
    label(left, "Drive (local)", &lv_font_montserrat_28, kAccent);
    ui.pos = label(left, "X 0  Y 0  Z 0");
    ui.driveState = label(left, "---", &lv_font_montserrat_20, kMuted);
    ui.env = label(left, "Env: ---");
    ui.tof = label(left, "ToF: ---");
    ui.gps = label(left, "GPS: off");
    label(left, "Gamepad jog speed", &lv_font_montserrat_16, kMuted);
    ui.jogSpeed = lv_slider_create(left);
    lv_obj_set_width(ui.jogSpeed, 300);
    lv_slider_set_range(ui.jogSpeed, 10, 100);
    lv_slider_set_value(ui.jogSpeed, controls_status().speedPercent, LV_ANIM_OFF);
    lv_obj_add_event_cb(ui.jogSpeed, jogSpeedChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    ui.jogSpeedLabel = label(left, "", &lv_font_montserrat_16, kMuted);

    lv_obj_t *right = grid(tab, 600, 480);
    // Same moves as HMI_MCU Drive pages 2-4.
    button(right, "Disk FW", kAccent, localCommand, (void *)"b45,-45");
    button(right, "Disk BW", kAccent, localCommand, (void *)"b-45,45");
    button(right, "Rot CW", kAccent, localCommand, (void *)"b45,45");
    button(right, "Rot CCW", kAccent, localCommand, (void *)"b-45,-45");
    button(right, "X +50", kMuted, localCommand, (void *)"b50,0");
    button(right, "X -50", kMuted, localCommand, (void *)"b-50,0");
    button(right, "Y +50", kMuted, localCommand, (void *)"b0,50");
    button(right, "Y -50", kMuted, localCommand, (void *)"b0,-50");
    ui.pidButton = button(right, "PID: OFF", kMuted, pidToggle, nullptr);
    button(right, "Rail +", kAccent, railAdjust, (void *)(intptr_t)1);
    button(right, "Rail -", kAccent, railAdjust, (void *)(intptr_t)-1);
    button(right, "Step +10", kMuted, zStepChange, (void *)(intptr_t)1, 150, 64);
    button(right, "HOME", kOff, localCommand, (void *)"home");
    button(right, "Enable", kOn, localCommand, (void *)"e");
    button(right, "Disable", kMuted, localCommand, (void *)"d");
    button(right, "Step -10", kMuted, zStepChange, (void *)(intptr_t)-1, 150, 64);
    ui.railLabel = label(right, "", &lv_font_montserrat_20, kMuted);
    updateRailLabels();
}

// ---- Robot / Cam / Sound tabs (boards that are still separate, through the PC) ------------------

lv_obj_t *statusColumn(lv_obj_t *tab, const char *title, lv_obj_t **online, lv_obj_t **info) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(tab, 12, 0);
    lv_obj_t *left = panel(tab, 360, 470);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    label(left, title, &lv_font_montserrat_28, kAccent);
    *online = label(left, "Offline", &lv_font_montserrat_20, kOff);
    *info = label(left, "---");
    lv_label_set_long_mode(*info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(*info, 330);
    return grid(tab, 600, 480);
}

void buildRobot(lv_obj_t *tab) {
    lv_obj_t *g = statusColumn(tab, "Robot3Axis", &ui.robotOnline, &ui.robotInfo);
    button(g, "X +50", kAccent, robotCommand, (void *)"rx50");
    button(g, "X -50", kAccent, robotCommand, (void *)"rx-50");
    button(g, "Y +50", kAccent, robotCommand, (void *)"ry50");
    button(g, "Y -50", kAccent, robotCommand, (void *)"ry-50");
    button(g, "Z +50", kAccent, robotCommand, (void *)"rz50");
    button(g, "Z -50", kAccent, robotCommand, (void *)"rz-50");
    button(g, "Grab open", kOn, robotCommand, (void *)"grab open");
    button(g, "Grab close", kOff, robotCommand, (void *)"grab close");
    button(g, "HOME", kMuted, robotCommand, (void *)"home");
    button(g, "X spd +", kMuted, robotCommand, (void *)"rxs10");
    button(g, "X spd -", kMuted, robotCommand, (void *)"rxs-10");
    button(g, "Y spd +", kMuted, robotCommand, (void *)"rys50");
    button(g, "Y spd -", kMuted, robotCommand, (void *)"rys-50");
    button(g, "Z spd +", kMuted, robotCommand, (void *)"rzs50");
    button(g, "Z spd -", kMuted, robotCommand, (void *)"rzs-50");
}

void viewImage(lv_event_t *) {
    pc_request_image();
    lv_label_set_text(ui.camImageNote, "Fetching image...");
}

void buildCam(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(tab, 12, 0);
    lv_obj_t *left = panel(tab, 300, 470);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(left, 8, 0);
    label(left, "Camera", &lv_font_montserrat_28, kAccent);
    ui.camOnline = label(left, "Offline", &lv_font_montserrat_20, kOff);
    ui.camInfo = label(left, "---", &lv_font_montserrat_16);
    lv_label_set_long_mode(ui.camInfo, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui.camInfo, 270);
    lv_obj_t *g = grid(left, 280, 220);
    lv_obj_set_style_pad_gap(g, 8, 0);
    button(g, "SHOT", kAccent, camCommand, (void *)"capture", 125, 56);
    button(g, "VIEW", kOn, viewImage, nullptr, 125, 56);
    button(g, "AI ANALYZE", kAccent, camCommand, (void *)"analyze", 125, 56);
    button(g, "STREAM", kMuted, camCommand, (void *)"stream", 125, 56);

    lv_obj_t *view = panel(tab, 660, 470);
    lv_obj_set_style_bg_color(view, lv_color_hex(0x101418), 0);
    ui.camImage = lv_image_create(view);
    lv_obj_center(ui.camImage);
    ui.camImageNote = label(view, "Press VIEW for the last captured image", &lv_font_montserrat_20, lv_color_white());
    lv_obj_align(ui.camImageNote, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void buildSound(lv_obj_t *tab) {
    lv_obj_t *g = statusColumn(tab, "Voice (ControlMCU)", &ui.soundOnline, &ui.soundInfo);
    button(g, "VAD", kOn, soundCommand, (void *)"vad");
    button(g, "PTT", kAccent, soundCommand, (void *)"ptt");
    button(g, "MUTE", kOff, soundCommand, (void *)"mute");
    button(g, "PLAY", kOn, soundCommand, (void *)"play");
}

// ---- System tab ------------------------------------------------------------------------------

void brightnessChanged(lv_event_t *) { bsp_backlight_set(lv_slider_get_value(ui.brightness)); }
void pairPad(lv_event_t *) { gamepad_pair(); }
void forgetPad(lv_event_t *) { gamepad_forget(); }
void modeToggle(lv_event_t *) { controls_set_mode(controls_status().mode == MODE_DRIVE ? MODE_ROBOT : MODE_DRIVE); }

lv_obj_t *qrPanel, *qrImage, *qrTitle, *qrNote;
lv_image_dsc_t qrDsc;
uint16_t *qrPixels;
bool qrWifi = true;

void closeSetupQr(lv_event_t *) { lv_obj_add_flag(qrPanel, LV_OBJ_FLAG_HIDDEN); }

void updateSetupQr() {
    const uint8_t *qr = ard_setup_qr(qrWifi)->modules;
    const int modules = ard_qrcodegen_getSize(qr), side = (modules + 8) * 4;
    if (!modules || side > 208) { lv_label_set_text(qrNote, "QR unavailable; use 192.168.4.1/network"); return; }
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            qrPixels[y * side + x] = ard_qrcodegen_getModule(qr, x / 4 - 4, y / 4 - 4) ? 0 : 0xffff;
    qrDsc = {};
    qrDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    qrDsc.header.cf = LV_COLOR_FORMAT_RGB565;
    qrDsc.header.w = side; qrDsc.header.h = side; qrDsc.header.stride = side * 2;
    qrDsc.data = reinterpret_cast<uint8_t *>(qrPixels); qrDsc.data_size = side * side * 2;
    lv_image_set_src(qrImage, &qrDsc);
    lv_obj_invalidate(qrImage);
    lv_label_set_text(qrTitle, qrWifi ? "1. Join setup Wi-Fi" : "2. Open setup page");
    lv_label_set_text_fmt(qrNote, "AP: %s\nPassword: %s\nLogin: admin / AP password\n%s",
                          ard_network_hostname(), ard_network_password(), ARD_SETUP_URL);
}

void nextSetupQr(lv_event_t *) { qrWifi = !qrWifi; updateSetupQr(); }

void showSetupQr(lv_event_t *) {
    if (!qrPanel) {
        qrPixels = static_cast<uint16_t *>(heap_caps_malloc(208 * 208 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!qrPixels) return;
        // Leave the permanent E-STOP bar above the popup accessible.
        qrPanel = panel(lv_layer_top(), 620, 470);
        lv_obj_set_pos(qrPanel, 202, 70);
        lv_obj_set_flex_flow(qrPanel, LV_FLEX_FLOW_COLUMN);
        qrTitle = label(qrPanel, "Setup QR", &lv_font_montserrat_28, kAccent);
        qrImage = lv_image_create(qrPanel);
        qrNote = label(qrPanel, "", &lv_font_montserrat_16);
        lv_obj_t *buttons = grid(qrPanel, 570, 56);
        lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
        button(buttons, "Next QR", kAccent, nextSetupQr, nullptr, 200, 48);
        button(buttons, "Close", kMuted, closeSetupQr, nullptr, 200, 48);
    }
    qrWifi = true;
    updateSetupQr();
    lv_obj_remove_flag(qrPanel, LV_OBJ_FLAG_HIDDEN);
}

void buildSystem(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(tab, 12, 0);
    lv_obj_t *net = panel(tab, 470, 470);
    lv_obj_set_flex_flow(net, LV_FLEX_FLOW_COLUMN);
    label(net, "Network", &lv_font_montserrat_28, kAccent);
    ui.netInfo = label(net, "---", &lv_font_montserrat_16);
    button(net, "Setup QR", kAccent, showSetupQr, nullptr, 200, 40);
    label(net, "Brightness", &lv_font_montserrat_16, kMuted);
    ui.brightness = lv_slider_create(net);
    lv_obj_set_width(ui.brightness, 400);
    lv_slider_set_range(ui.brightness, 5, 100);
    lv_slider_set_value(ui.brightness, bsp_backlight_get(), LV_ANIM_OFF);
    lv_obj_add_event_cb(ui.brightness, brightnessChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    ui.sysInfo = label(net, "", &lv_font_montserrat_16, kMuted);

    lv_obj_t *pad = panel(tab, 490, 470);
    lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_COLUMN);
    label(pad, "Gamepad (BLE via C6)", &lv_font_montserrat_28, kAccent);
    ui.padInfo = label(pad, "---", &lv_font_montserrat_16);
    ui.padRaw = label(pad, "", &lv_font_montserrat_16, kMuted);
    lv_obj_t *g = grid(pad, 460, 90);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
    button(g, "Pair new", kAccent, pairPad, nullptr, 140, 56);
    button(g, "Forget", kOff, forgetPad, nullptr, 140, 56);
    button(g, "Drive/Robot", kMuted, modeToggle, nullptr, 150, 56);
}

// ---- status bar + popup ------------------------------------------------------------------------

void approvalAnswer(lv_event_t *e) {
    const bool approve = (intptr_t)lv_event_get_user_data(e) != 0;
    pc_send_approval(ui.approvalId, approve);
    lv_obj_add_flag(ui.approval, LV_OBJ_FLAG_HIDDEN);
}

void buildApproval() {
    ui.approval = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ui.approval, 600, 320);
    lv_obj_center(ui.approval);
    lv_obj_set_style_border_color(ui.approval, kAccent, 0);
    lv_obj_set_style_border_width(ui.approval, 4, 0);
    lv_obj_t *title = label(ui.approval, "AI Command - approve?", &lv_font_montserrat_28, kAccent);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    ui.approvalText = label(ui.approval, "", &lv_font_montserrat_20);
    lv_label_set_long_mode(ui.approvalText, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui.approvalText, 540);
    lv_obj_align(ui.approvalText, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_t *ok = button(ui.approval, "APPROVE", kOn, approvalAnswer, (void *)(intptr_t)1, 220, 80);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_LEFT, 20, 0);
    lv_obj_t *no = button(ui.approval, "REJECT", kOff, approvalAnswer, (void *)(intptr_t)0, 220, 80);
    lv_obj_align(no, LV_ALIGN_BOTTOM_RIGHT, -20, 0);
    lv_obj_add_flag(ui.approval, LV_OBJ_FLAG_HIDDEN);
}

void buildBar(lv_obj_t *screen) {
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_set_size(bar, LV_PCT(100), 72);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, kBar, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = label(bar, "ARD", &lv_font_montserrat_28, lv_color_white());
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    ui.clock = label(bar, "--:--", &lv_font_montserrat_28, lv_color_white());
    lv_obj_align(ui.clock, LV_ALIGN_LEFT_MID, 80, 0);
    ui.net = label(bar, "Wi-Fi --", &lv_font_montserrat_16, lv_color_white());
    lv_obj_align(ui.net, LV_ALIGN_LEFT_MID, 190, -12);
    ui.pc = label(bar, "PC --", &lv_font_montserrat_16, lv_color_white());
    lv_obj_align(ui.pc, LV_ALIGN_LEFT_MID, 190, 12);
    ui.pad = label(bar, "Pad --", &lv_font_montserrat_16, lv_color_white());
    lv_obj_align(ui.pad, LV_ALIGN_LEFT_MID, 430, -12);
    ui.armed = label(bar, "", &lv_font_montserrat_16, lv_color_white());
    lv_obj_align(ui.armed, LV_ALIGN_LEFT_MID, 430, 12);
    lv_obj_t *arm = button(bar, "RE-ARM", kMuted, rearm, nullptr, 120, 56);
    lv_obj_align(arm, LV_ALIGN_RIGHT_MID, -200, 0);
    lv_obj_t *stop = button(bar, "E-STOP", kOff, estop, nullptr, 180, 60);
    lv_obj_set_style_text_font(lv_obj_get_child(stop, 0), &lv_font_montserrat_28, 0);
    lv_obj_align(stop, LV_ALIGN_RIGHT_MID, 0, 0);
}

// ---- periodic refresh ---------------------------------------------------------------------------

const char *linkName(GamepadLink link) {
    switch (link) {
    case GAMEPAD_CONNECTED: return "connected";
    case GAMEPAD_CONNECTING: return "connecting";
    case GAMEPAD_PAIRING: return "pairing";
    case GAMEPAD_SEARCHING: return "searching";
    default: return "off";
    }
}

void showBoard(lv_obj_t *online, const PcBoardState &b, bool reachable) {
    lv_label_set_text_fmt(online, "%s  %s", !reachable ? "PC offline" : b.online ? "Online" : "Offline", b.ip);
    lv_obj_set_style_text_color(online, reachable && b.online ? kOn : kOff, 0);
}

void refreshCamImage() {
    PcImage img;
    if (!pc_image_lock(&img, 5)) return;
    if (img.sequence != imageSeq) {
        imageSeq = img.sequence;
        if (img.pixels) {
            const size_t bytes = (size_t)img.width * img.height * 2;
            if (bytes > imageCopySize) {
                heap_caps_free(imageCopy);
                imageCopy = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
                imageCopySize = imageCopy ? bytes : 0;
            }
            if (imageCopy) {
                memcpy(imageCopy, img.pixels, bytes);
                imageDsc = {};
                imageDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
                imageDsc.header.cf = LV_COLOR_FORMAT_RGB565;
                imageDsc.header.w = img.width;
                imageDsc.header.h = img.height;
                imageDsc.header.stride = img.width * 2;
                imageDsc.data = imageCopy;
                imageDsc.data_size = bytes;
                lv_image_set_src(ui.camImage, &imageDsc);
                // Fit into the 630x440 viewer.
                uint32_t scale = 256;
                if (img.width > 630 || img.height > 440) {
                    const uint32_t sx = 630 * 256 / img.width, sy = 440 * 256 / img.height;
                    scale = sx < sy ? sx : sy;
                }
                lv_image_set_scale(ui.camImage, scale);
                lv_obj_center(ui.camImage);
                lv_label_set_text_fmt(ui.camImageNote, "%ux%u", img.width, img.height);
            }
        } else {
            lv_label_set_text(ui.camImageNote, img.error);
        }
    }
    pc_image_unlock();
}

void refresh(lv_timer_t *) {
    if (pendingTabStep) {
        const int count = TAB_COUNT;
        int next = ((int)lv_tabview_get_tab_active(ui.tabview) + pendingTabStep + count) % count;
        pendingTabStep = 0;
        lv_tabview_set_active(ui.tabview, next, LV_ANIM_OFF);
    }
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    if (local.tm_year >= 120) lv_label_set_text_fmt(ui.clock, "%02d:%02d", local.tm_hour, local.tm_min);

    char ip[16];
    ard_network_local_ip(ip, sizeof(ip));
    if (ard_network_connected()) lv_label_set_text_fmt(ui.net, "Wi-Fi %s  %d dBm", ip, ard_network_rssi());
    else lv_label_set_text(ui.net, "Wi-Fi offline (setup AP on)");
    const PcStatus pc = pc_status();
    char server[65];
    ard_network_server(server, sizeof(server));
    lv_label_set_text_fmt(ui.pc, "PC %s %s", server, pc.reachable ? "OK" : "--");

    const GamepadInfo pad = gamepad_info();
    const ControlStatus ctl = controls_status();
    lv_label_set_text_fmt(ui.pad, "Pad %s  %s", linkName(pad.link), ctl.mode == MODE_DRIVE ? "DRIVE" : "ROBOT");

    MotionStatus m = {};
    const bool haveMotion = motion_status(&m, 5);
    lv_label_set_text(ui.armed, m.stopped ? "STOPPED - press RE-ARM" : ctl.lastAction);
    lv_obj_set_style_text_color(ui.armed, m.stopped ? lv_color_hex(0xFF8A80) : lv_color_white(), 0);

    switch (lv_tabview_get_tab_active(ui.tabview)) {
    case TAB_DRIVE: {
        if (haveMotion) {
            lv_label_set_text_fmt(ui.pos, "X %ld   Y %ld   Z %ld", (long)m.x, (long)m.y, (long)m.z);
            lv_label_set_text_fmt(ui.driveState, "%s%s%s%s  queue %lu  last move %lu ms",
                                  m.stopped ? "STOPPED " : "", m.busy ? "BUSY " : "IDLE ", m.jogging ? "JOG " : "",
                                  m.pid ? "PID" : "", (unsigned long)m.queued, (unsigned long)m.lastMoveMs);
        }
        const EnvironmentSample env = sensors_environment();
        char t[24] = "N/A", h[24] = "N/A", p[24] = "N/A";
        if (env.ahtValid) { snprintf(t, sizeof(t), "%.1f C", env.temperature); snprintf(h, sizeof(h), "%.0f %%", env.humidity); }
        if (env.bmpValid) snprintf(p, sizeof(p), "%.1f hPa", env.pressure);
        lv_label_set_text_fmt(ui.env, "Env: %s  %s  %s", t, h, p);
        lv_label_set_text_fmt(ui.tof, "ToF: %.0f mm (%s)", sensors_tof_last(), sensors_tof_status());
        const GpsSnapshot g = gps_snapshot();
        if (!g.enabled) lv_label_set_text(ui.gps, "GPS: off (gps1 to enable)");
        else if (g.fix) lv_label_set_text_fmt(ui.gps, "GPS: %.5f, %.5f (%lu sat)", g.lat, g.lng, (unsigned long)g.satellites);
        else lv_label_set_text_fmt(ui.gps, "GPS: searching (%lu chars)", (unsigned long)g.chars);
        lv_label_set_text_fmt(ui.jogSpeedLabel, "%d %%%s", ctl.speedPercent, ctl.precision ? " (precision)" : "");
        if (lv_slider_get_value(ui.jogSpeed) != ctl.speedPercent && !lv_obj_has_state(ui.jogSpeed, LV_STATE_PRESSED))
            lv_slider_set_value(ui.jogSpeed, ctl.speedPercent, LV_ANIM_OFF);
        pidOn = m.pid;
        updateRailLabels();
        break;
    }
    case TAB_ROBOT:
        showBoard(ui.robotOnline, pc.board[PC_ROBOT], pc.reachable);
        lv_label_set_text_fmt(ui.robotInfo, "X  %.1f deg\nY  %.1f mm\nZ  %.1f mm\nGrip  %s\nSpeed  %d / %d / %d\nLast seen %s",
                              pc.robotX, pc.robotY, pc.robotZ, pc.robotGrab, pc.robotSpeed[0], pc.robotSpeed[1],
                              pc.robotSpeed[2], pc.board[PC_ROBOT].lastSeen);
        break;
    case TAB_CAM:
        showBoard(ui.camOnline, pc.board[PC_CAM], pc.reachable);
        lv_label_set_text_fmt(ui.camInfo, "Sensor %s\nRes %s  Q %d\nAI model %s\n\nObject: %s\nCategory: %s",
                              pc.camSensor, pc.camRes, pc.camQuality, pc.aiModel, pc.aiObject, pc.aiCategory);
        refreshCamImage();
        break;
    case TAB_SOUND:
        showBoard(ui.soundOnline, pc.board[PC_SOUND], pc.reachable);
        lv_label_set_text_fmt(ui.soundInfo, "Mode  %s\nStatus  %s\nToggle mode  %s", pc.soundMode, pc.soundStatus,
                              pc.controlMode);
        break;
    case TAB_SYSTEM: {
        const ard_network_settings_t *s = ard_network_settings();
        lv_label_set_text_fmt(ui.netInfo,
                              "Configured SSID: %s\nIP: %s\nPC server: %s (%s)\n\nSetup AP: %s\nAP password: %s\n"
                              "http://192.168.4.1/network",
                              s->ssid[0] ? s->ssid : "(not set)", ip, s->server, server, ard_network_hostname(),
                              ard_network_password());
        lv_label_set_text_fmt(ui.sysInfo, "Heap %lu KB internal, %lu KB PSRAM\nTouch %s",
                              (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                              (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                              bsp_touch_ready() ? "OK" : "not found");
        if (pad.link == GAMEPAD_CONNECTED)
            lv_label_set_text_fmt(ui.padInfo, "%s\n%s  reports %lu  battery %d%%", pad.name, pad.address,
                                  (unsigned long)pad.reports, pad.battery);
        else
            lv_label_set_text_fmt(ui.padInfo, "%s\nPair: hold the pad's pairing button,\nthen press 'Pair new'.",
                                  linkName(pad.link));
        GamepadState st;
        if (gamepad_state(&st)) {
            char pressed[96] = "";
            for (int i = 0; i < 32; ++i)
                if (st.buttons & (1u << i)) {
                    char n[6];
                    snprintf(n, sizeof(n), "%d ", i + 1);
                    strlcat(pressed, n, sizeof(pressed));
                }
            lv_label_set_text_fmt(ui.padRaw, "L %.2f %.2f  R %.2f %.2f\nLT %.2f RT %.2f hat %d\nbuttons: %s", st.lx,
                                  st.ly, st.rx, st.ry, st.lt, st.rt, st.hat, pressed);
        } else {
            lv_label_set_text(ui.padRaw, "");
        }
        break;
    }
    }

    if (pc.approvalPending && lv_obj_has_flag(ui.approval, LV_OBJ_FLAG_HIDDEN)) {
        strlcpy(ui.approvalId, pc.approvalId, sizeof(ui.approvalId));
        lv_label_set_text(ui.approvalText, pc.approvalDesc);
        lv_obj_clear_flag(ui.approval, LV_OBJ_FLAG_HIDDEN);
    } else if (!pc.approvalPending && pc.reachable && !lv_obj_has_flag(ui.approval, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(ui.approval, LV_OBJ_FLAG_HIDDEN); // answered elsewhere (PC dashboard)
    }
}

void tabStep(int direction) { pendingTabStep = direction; } // applied by the LVGL timer

} // namespace

esp_err_t ui_start() {
    if (!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    buildBar(screen);
    ui.tabview = lv_tabview_create(screen);
    lv_tabview_set_tab_bar_size(ui.tabview, 56);
    lv_obj_set_size(ui.tabview, LCD_H_RES, LCD_V_RES - 72);
    lv_obj_align(ui.tabview, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(lv_tabview_get_tab_bar(ui.tabview), &lv_font_montserrat_20, 0);
    buildDrive(lv_tabview_add_tab(ui.tabview, "Drive"));
    buildRobot(lv_tabview_add_tab(ui.tabview, "Robot"));
    buildCam(lv_tabview_add_tab(ui.tabview, "Cam"));
    buildSound(lv_tabview_add_tab(ui.tabview, "Voice"));
    buildSystem(lv_tabview_add_tab(ui.tabview, "System"));
    buildApproval();
    lv_timer_create(refresh, 250, nullptr);
    bsp_lvgl_unlock();
    controls_on_tab_step(tabStep);
    return ESP_OK;
}
