#include "ui.h"
#include "ui_main.h"
#include "ui_settings.h"
#include "ui_text.h"
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
#include "onboard_audio.h"
#include "pc_link.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"

namespace {

// Light theme close to the old HMI palette, with stronger contrast for a 7" panel.
const lv_color_t kBar = lv_color_hex(0x1F2A36), kAccent = lv_color_hex(0x0B6E99), kOn = lv_color_hex(0x1E9E50),
                 kOff = lv_color_hex(0xB3261E), kMuted = lv_color_hex(0x5F6B76), kPanel = lv_color_hex(0xF2F4F6);

enum Tab { TAB_MAIN, TAB_DRIVE, TAB_CAM, TAB_SOUND, TAB_SYSTEM, TAB_SETTINGS, TAB_COUNT };

struct Ui {
    lv_obj_t *tabview;
    lv_obj_t *clock, *net, *pc, *pad, *armed;
    // Drive
    lv_obj_t *pos, *driveState, *env, *tof, *gps, *jogSpeed, *jogSpeedLabel, *pidButton, *railLabel, *stepLabel;
    // Cam / Sound
    lv_obj_t *camOnline, *camInfo, *camImage, *camImageNote, *camStream, *soundOnline, *soundInfo;
    lv_obj_t *talk, *micToggle;
    // System
    lv_obj_t *netInfo, *padInfo, *padRaw, *sysInfo;
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

lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font = &ard_font_20,
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
    lv_obj_t *l = label(b, text, &ard_font_20, lv_color_white());
    lv_obj_center(l);
    return b;
}

lv_obj_t *panel(lv_obj_t *parent, int w, int h) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, kPanel, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
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
    command_execute(cmd); // PC routes to the unchanged, independent DriveMCU
}

void camCommand(lv_event_t *e) { pc_send_command(PC_CAM, static_cast<const char *>(lv_event_get_user_data(e))); }
void soundCommand(lv_event_t *e) {
    const char *cmd = static_cast<const char *>(lv_event_get_user_data(e));
    onboard_audio_command(!strcmp(cmd, "stop") ? "audio_stop" : cmd); // immediate local feedback; the PC remains authoritative for mode/volume.
    pc_send_command(PC_SOUND, cmd);
}
void talkEvent(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) onboard_audio_set_ptt(true);
    else if (lv_event_get_code(e) == LV_EVENT_RELEASED || lv_event_get_code(e) == LV_EVENT_PRESS_LOST)
        onboard_audio_set_ptt(false);
}
void micToggle(lv_event_t *) { onboard_audio_set_mic(!onboard_audio_status().micEnabled); }
void speakerTest(lv_event_t *) { onboard_audio_test(); }

void estop(lv_event_t *) { command_execute("stop"); }
void rearm(lv_event_t *) { pc_drive_arm(); }

// ---- Drive tab ---------------------------------------------------------------------------------

void updateRailLabels() {
    char text[48];
    snprintf(text, sizeof(text), ui_tr(pidOn ? "PID target: %d mm" : "Z step: %d steps"), pidOn ? railTarget : zStep);
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
    label(left, "DriveMCU (via PC)", &ard_font_28, kAccent);
    ui.pos = label(left, "X 0  Y 0  Z 0");
    ui.driveState = label(left, "---", &ard_font_20, kMuted);
    ui.env = label(left, "Env: ---");
    ui.tof = label(left, "ToF: ---");
    ui.gps = label(left, "GPS: off");
    label(left, "Gamepad jog speed", &ard_font_16, kMuted);
    ui.jogSpeed = lv_slider_create(left);
    lv_obj_set_width(ui.jogSpeed, 300);
    lv_slider_set_range(ui.jogSpeed, 10, 100);
    lv_slider_set_value(ui.jogSpeed, controls_status().speedPercent, LV_ANIM_OFF);
    lv_obj_add_event_cb(ui.jogSpeed, jogSpeedChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    ui.jogSpeedLabel = label(left, "", &ard_font_16, kMuted);

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
    ui.railLabel = label(right, "", &ard_font_20, kMuted);
    updateRailLabels();
}

// ---- Cam / Sound tabs (boards that are still separate, through the PC) --------------------------

lv_obj_t *statusColumn(lv_obj_t *tab, const char *title, lv_obj_t **online, lv_obj_t **info) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(tab, 12, 0);
    lv_obj_t *left = panel(tab, 360, 470);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    label(left, title, &ard_font_28, kAccent);
    *online = label(left, "Offline", &ard_font_20, kOff);
    *info = label(left, "---");
    lv_label_set_long_mode(*info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(*info, 330);
    return grid(tab, 600, 480);
}

// The PC drives the preview (ImageSender only knows single captures); ask for the opposite state.
void streamToggle(lv_event_t *) { pc_send_command(PC_CAM, pc_status().cameraStream ? "stream_off" : "stream_on"); }

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
    label(left, "Camera", &ard_font_28, kAccent);
    ui.camOnline = label(left, "Offline", &ard_font_20, kOff);
    ui.camInfo = label(left, "---", &ard_font_16);
    lv_label_set_long_mode(ui.camInfo, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui.camInfo, 270);
    lv_obj_t *g = grid(left, 280, 220);
    lv_obj_set_style_pad_gap(g, 8, 0);
    button(g, "SHOT", kAccent, camCommand, (void *)"capture", 125, 56);
    button(g, "VIEW", kOn, viewImage, nullptr, 125, 56);
    button(g, "AI ANALYZE", kAccent, camCommand, (void *)"analyze", 125, 56);
    ui.camStream = button(g, "STREAM", kMuted, streamToggle, nullptr, 125, 56);

    lv_obj_t *view = panel(tab, 660, 470);
    lv_obj_set_style_bg_color(view, lv_color_hex(0x101418), 0);
    ui.camImage = lv_image_create(view);
    lv_obj_center(ui.camImage);
    ui.camImageNote = label(view, "New captures appear here automatically", &ard_font_20, lv_color_white());
    lv_obj_align(ui.camImageNote, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void buildSound(lv_obj_t *tab) {
    lv_obj_t *g = statusColumn(tab, "Onboard voice", &ui.soundOnline, &ui.soundInfo);
    button(g, "VAD", kOn, soundCommand, (void *)"vad", 175);
    button(g, "PTT", kAccent, soundCommand, (void *)"ptt", 175);
    ui.micToggle = button(g, "MIC ON", kMuted, micToggle, nullptr, 175);
    ui.talk = button(g, "HOLD TO TALK (PTT)", kAccent, talkEvent, nullptr, 550, 84);
    lv_obj_add_event_cb(ui.talk, talkEvent, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(ui.talk, talkEvent, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(ui.talk, talkEvent, LV_EVENT_PRESS_LOST, nullptr);
    button(g, "VOL -", kMuted, soundCommand, (void *)"vol-", 175);
    button(g, "VOL +", kMuted, soundCommand, (void *)"vol+", 175);
    button(g, "MUTE", kOff, soundCommand, (void *)"mute", 175);
    button(g, "SPEAKER TEST", kAccent, speakerTest, nullptr, 175);
    button(g, "PLAY", kOn, soundCommand, (void *)"play", 175);
    button(g, "STOP AUDIO", kOff, soundCommand, (void *)"stop", 175);
}

// ---- System tab ------------------------------------------------------------------------------

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
    lv_label_set_text_fmt(qrNote, "AP: %s\nOpen Wi-Fi / No login required\n%s",
                          ard_network_hostname(), ARD_SETUP_URL);
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
        qrTitle = label(qrPanel, "Setup QR", &ard_font_28, kAccent);
        qrImage = lv_image_create(qrPanel);
        qrNote = label(qrPanel, "", &ard_font_16);
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
    label(net, "Network", &ard_font_28, kAccent);
    ui.netInfo = label(net, "---", &ard_font_16);
    button(net, "Setup QR", kAccent, showSetupQr, nullptr, 200, 40);
    ui.sysInfo = label(net, "", &ard_font_16, kMuted);

    lv_obj_t *pad = panel(tab, 490, 470);
    lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_COLUMN);
    label(pad, "Gamepad (BLE via C6)", &ard_font_28, kAccent);
    ui.padInfo = label(pad, "---", &ard_font_16);
    ui.padRaw = label(pad, "", &ard_font_16, kMuted);
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
    lv_obj_t *title = label(ui.approval, "AI Command - approve?", &ard_font_28, kAccent);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    ui.approvalText = label(ui.approval, "", &ard_font_20);
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
    lv_obj_t *title = label(bar, "ARD", &ard_font_28, lv_color_white());
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    ui.clock = label(bar, "--:--", &ard_font_28, lv_color_white());
    lv_obj_align(ui.clock, LV_ALIGN_LEFT_MID, 80, 0);
    ui.net = label(bar, "Wi-Fi --", &ard_font_16, lv_color_white());
    lv_obj_align(ui.net, LV_ALIGN_LEFT_MID, 190, -12);
    ui.pc = label(bar, "PC --", &ard_font_16, lv_color_white());
    lv_obj_align(ui.pc, LV_ALIGN_LEFT_MID, 190, 12);
    ui.pad = label(bar, "Pad --", &ard_font_16, lv_color_white());
    lv_obj_align(ui.pad, LV_ALIGN_LEFT_MID, 430, -12);
    ui.armed = label(bar, "", &ard_font_16, lv_color_white());
    lv_obj_align(ui.armed, LV_ALIGN_LEFT_MID, 430, 12);
    lv_obj_t *arm = button(bar, "RE-ARM", kMuted, rearm, nullptr, 120, 56);
    lv_obj_align(arm, LV_ALIGN_RIGHT_MID, -200, 0);
    lv_obj_t *stop = button(bar, "E-STOP", kOff, estop, nullptr, 180, 60);
    lv_obj_set_style_text_font(lv_obj_get_child(stop, 0), &ard_font_28, 0);
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
    lv_label_set_text_fmt(online, "%s  %s", ui_tr(!reachable ? "PC offline" : b.online ? "Online" : "Offline"), b.ip);
    lv_obj_set_style_text_color(online, reachable && b.online ? kOn : kOff, 0);
}

void refreshCamImage() {
    PcImage img;
    if (!pc_image_lock(&img, 5)) return;
    if (img.sequence != imageSeq) {
        imageSeq = img.sequence;
        if (img.pixels) {
            const size_t bytes = (size_t)img.stride * img.height * 2;
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
                imageDsc.header.stride = img.stride * 2;
                imageDsc.data = imageCopy;
                imageDsc.data_size = bytes;
                lv_image_set_src(ui.camImage, &imageDsc);
                // Fill the 630x440 viewer, enlarging small captures too (aspect kept).
                const uint32_t sx = 630 * 256 / img.width, sy = 440 * 256 / img.height;
                lv_image_set_scale(ui.camImage, sx < sy ? sx : sy);
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
    else lv_label_set_text(ui.net, ard_network_setup_ap_active() ? "Wi-Fi offline (setup AP on)" : "Wi-Fi offline");
    const PcStatus pc = pc_status();
    ui_settings_poll(lv_tabview_get_tab_active(ui.tabview) == TAB_SETTINGS && !pc.approvalPending);
    ui_main_poll(pc, lv_tabview_get_tab_active(ui.tabview) == TAB_MAIN);
    char server[65];
    ard_network_server(server, sizeof(server));
    lv_label_set_text_fmt(ui.pc, "PC %s %s", server, pc.reachable ? ui_tr("OK") : "--");

    const GamepadInfo pad = gamepad_info();
    const ControlStatus ctl = controls_status();
    lv_label_set_text_fmt(ui.pad, "Pad %s  %s", ui_tr(linkName(pad.link)), ui_tr(ctl.mode == MODE_DRIVE ? "DRIVE" : "ROBOT"));

    lv_label_set_text(ui.armed, ctl.lastAction);
    lv_obj_set_style_text_color(ui.armed, lv_color_white(), 0);

    switch (lv_tabview_get_tab_active(ui.tabview)) {
    case TAB_DRIVE: {
        const bool online = pc.reachable && pc.board[PC_DRIVE].online;
        if (online) lv_label_set_text_fmt(ui.pos, "PC estimate: X %ld   Y %ld   Z %ld",
            (long)pc.driveX, (long)pc.driveY, (long)pc.driveZ);
        else lv_label_set_text(ui.pos, "DriveMCU offline / PC unavailable");
        lv_label_set_text_fmt(ui.driveState, "DriveMCU %s  %s", pc.board[PC_DRIVE].ip, ui_tr(online ? "ONLINE" : "OFFLINE"));
        lv_label_set_text_fmt(ui.env, "Drive env: %.1f C  %.0f %%  %.1f hPa", pc.driveTemp, pc.driveHumidity, pc.drivePressure);
        lv_label_set_text_fmt(ui.tof, "Drive ToF: %.0f mm", pc.driveTof);
        lv_label_set_text_fmt(ui.gps, "Drive GPS: %.5f, %.5f", pc.driveLat, pc.driveLng);
        lv_label_set_text_fmt(ui.jogSpeedLabel, "%d %%%s", ctl.speedPercent, ctl.precision ? ui_tr(" (precision)") : "");
        if (lv_slider_get_value(ui.jogSpeed) != ctl.speedPercent && !lv_obj_has_state(ui.jogSpeed, LV_STATE_PRESSED))
            lv_slider_set_value(ui.jogSpeed, ctl.speedPercent, LV_ANIM_OFF);
        pidOn = pc.drivePid;
        updateRailLabels();
        break;
    }
    case TAB_CAM:
        showBoard(ui.camOnline, pc.board[PC_CAM], pc.reachable);
        lv_label_set_text(lv_obj_get_child(ui.camStream, 0), pc.cameraStream ? "STOP STREAM" : "STREAM");
        lv_obj_set_style_bg_color(ui.camStream, pc.cameraStream ? kOff : kMuted, 0);
        lv_label_set_text_fmt(ui.camInfo, "Sensor %s\nRes %s  Q %d\nAI model %s\n\nObject: %s\nCategory: %s",
                              pc.camSensor, pc.camRes, pc.camQuality, pc.aiModel, pc.aiObject, pc.aiCategory);
        refreshCamImage();
        break;
    case TAB_SOUND: {
        const auto audio = onboard_audio_status();
        lv_label_set_text(ui.soundOnline, !audio.ready ? "ES8311 unavailable" : !pc.reachable ? "PC offline" : !audio.selected ? "Select JC1060 voice on PC" : "Onboard microphone / speaker");
        lv_obj_set_style_text_color(ui.soundOnline, audio.ready && audio.selected ? kOn : kOff, 0);
        const char *state = audio.recording ? "RECORDING" : audio.uploading ? "UPLOADING" : audio.waiting ? "PROCESSING" :
                            audio.playing ? "SPEAKING" : audio.micEnabled && audio.selected ? "LISTENING" : "MIC OFF";
        lv_label_set_text_fmt(ui.soundInfo, "%s  %s\nMode %s\nMic level %d / 32767\nVolume %d / 21%s\nClips %lu  errors %lu\n\nPTT: hold TALK or gamepad RT\nVAD: automatic speech input\n\n%s",
            ui_tr(state), pc.soundStatus, ui_tr(audio.vad ? "VAD" : "PTT"), audio.peak, audio.volume,
            audio.muted ? ui_tr(" (muted)") : "",
            (unsigned long)audio.uploadedClips, (unsigned long)audio.uploadErrors, audio.error);
        lv_label_set_text(lv_obj_get_child(ui.micToggle, 0), audio.micEnabled ? "MIC ON" : "MIC OFF");
        const bool canTalk = audio.ready && audio.selected && audio.micEnabled && !audio.vad &&
                             !audio.uploading && !audio.waiting && !audio.playing;
        // Keep the pressed button enabled until release so a recording always gets its stop event.
        if (canTalk || audio.recording) lv_obj_remove_state(ui.talk, LV_STATE_DISABLED);
        else lv_obj_add_state(ui.talk, LV_STATE_DISABLED);
        break;
    }
    case TAB_SYSTEM: {
        ard_network_settings_t snapshot;
        ard_network_get_settings(&snapshot);
        const ard_network_settings_t *s = &snapshot;
        lv_label_set_text_fmt(ui.netInfo,
                              "Configured SSID: %s\nIP: %s\nPC server: %s (%s)\n\nSetup AP: %s\nOpen Wi-Fi / No login required\n"
                              "http://192.168.4.1/network",
                              s->ssid[0] ? s->ssid : ui_tr("(not set)"), ip, s->server, server, ard_network_hostname());
        lv_label_set_text_fmt(ui.sysInfo, "Heap %lu KB internal, %lu KB PSRAM\nTouch %s",
                              (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                              (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                              ui_tr(bsp_touch_ready() ? "OK" : "not found"));
        if (pad.link == GAMEPAD_CONNECTED)
            lv_label_set_text_fmt(ui.padInfo, "%s\n%s  reports %lu  battery %d%%", pad.name, pad.address,
                                  (unsigned long)pad.reports, pad.battery);
        else
            lv_label_set_text_fmt(ui.padInfo, "%s\nPair: hold the pad's pairing button,\nthen press 'Pair new'.",
                                  ui_tr(linkName(pad.link)));
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

void ui_notice(const char *text) {
    static lv_obj_t *notice, *noticeText;
    if (!bsp_lvgl_lock(1000)) return;
    if (!notice && text) {
        notice = lv_obj_create(lv_layer_top());
        lv_obj_set_size(notice, 700, 200);
        lv_obj_center(notice);
        lv_obj_set_style_bg_color(notice, kBar, 0);
        lv_obj_set_style_border_width(notice, 0, 0);
        lv_obj_clear_flag(notice, LV_OBJ_FLAG_SCROLLABLE);
        noticeText = label(notice, "", &ard_font_28, lv_color_white());
        lv_label_set_long_mode(noticeText, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(noticeText, 640);
        lv_obj_set_style_text_align(noticeText, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(noticeText);
    }
    if (notice) {
        if (text) {
            lv_label_set_text(noticeText, text);
            lv_obj_remove_flag(notice, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(notice, LV_OBJ_FLAG_HIDDEN);
        }
    }
    bsp_lvgl_unlock();
}

esp_err_t ui_start() {
    if (!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;
    ui_text_init(); // language and Hangul-capable fonts, before any label
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    buildBar(screen);
    ui.tabview = lv_tabview_create(screen);
    lv_tabview_set_tab_bar_size(ui.tabview, 56);
    lv_obj_set_size(ui.tabview, LCD_H_RES, LCD_V_RES - 72);
    lv_obj_align(ui.tabview, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(lv_tabview_get_tab_bar(ui.tabview), &ard_font_20, 0);
    // Main (sorting sequence) is first, so it is the screen shown after boot.
    ui_main_create(lv_tabview_add_tab(ui.tabview, "Main"));
    buildDrive(lv_tabview_add_tab(ui.tabview, "Drive"));
    buildCam(lv_tabview_add_tab(ui.tabview, "Cam"));
    buildSound(lv_tabview_add_tab(ui.tabview, "Voice"));
    buildSystem(lv_tabview_add_tab(ui.tabview, "System"));
    lv_obj_t *settingsPage = lv_tabview_add_tab(ui.tabview, "Settings");
    const esp_err_t settingsError = ui_settings_create(settingsPage);
    if (settingsError != ESP_OK) label(settingsPage, "Settings could not start. Restart the board.");
    // Make page coverage explicit when switching away from buttons to the dark
    // camera viewer; the default tabview page style is transparent in LVGL 9.2.
    lv_obj_t *content = lv_tabview_get_content(ui.tabview);
    lv_obj_set_style_bg_color(content, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    for (int i = 0; i < TAB_COUNT; ++i) {
        lv_obj_t *page = lv_obj_get_child(content, i);
        lv_obj_set_style_bg_color(page, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    }
    buildApproval();
    lv_timer_create(refresh, 250, nullptr);
    bsp_lvgl_unlock();
    controls_on_tab_step(tabStep);
    return ESP_OK;
}
