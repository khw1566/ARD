#include "ui.h"
#include "ui_main.h"
#include "ui_settings.h"
#include "ui_screensaver.h"
#include "visitor_voice.h"
#include "runtime_health.h"
#include "ui_layout.h"
#include "ui_text.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <cmath>
#include <initializer_list>
#include "ard_network.h"
#include "ard_setup_qr.h"
#include "board.h"
#include "bsp_display.h"
#include "commands.h"
#include "onboard_audio.h"
#include "pc_link.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"

namespace {

// Light theme close to the old HMI palette, with stronger contrast for a 7" panel.
const lv_color_t kBar = lv_color_hex(0x1F2A36), kAccent = lv_color_hex(0x0B6E99), kOn = lv_color_hex(0x1E9E50),
                 kOff = lv_color_hex(0xB3261E), kMuted = lv_color_hex(0x5F6B76), kPanel = lv_color_hex(0xF2F4F6);

enum Tab { TAB_MAIN, TAB_DRIVE, TAB_RAIL, TAB_CAM, TAB_SOUND, TAB_SYSTEM, TAB_SETTINGS, TAB_COUNT };

struct Ui {
    lv_obj_t *tabview;
    lv_obj_t *railMode, *railModeText, *railLive, *railGains, *railTargetSlider, *railTargetText, *railMessage;
    lv_obj_t *railPidArea, *railOpenArea, *railStart, *railStop, *railPresets[4];
    lv_obj_t *clock, *net, *pc;
    // Drive
    lv_obj_t *pos, *driveState, *env, *tof, *gps, *pidButton, *railLabel, *stepLabel;
    lv_obj_t *driveMode, *driveSteps, *driveContinuous, *driveControlState;
    lv_obj_t *driveStepSettings, *driveContinuousSettings, *driveDeadzone, *deadzoneValue;
    lv_obj_t *joystick, *joystickKnob, *joystickDeadzone, *joystickLevel, *driveInterval, *driveSpeed;
    // Cam / Sound
    lv_obj_t *camOnline, *camInfo, *camImage, *camViewport, *camImageNote, *camStream, *soundOnline, *soundInfo;
    lv_obj_t *talk, *micToggle, *micToggleText, *micState;
    // System
    lv_obj_t *netInfo, *sysInfo;
    // Approval popup
    lv_obj_t *approval, *approvalText;
    char approvalId[40];
};

Ui ui;
int railTarget = 120, unitStep = 50; // finite quantitative moves, 1..2000 steps
bool pidOn = false;
bool noticeVisible = false;
bool railRequestPending = false, railDraft = true;
uint32_t railRequestSequence = 0, railRequestStart = 0;
// After a local PID/target change the PC still reports the old board state for up to ~1 s.
constexpr uint32_t kRailHoldMs = 1500;
uint32_t railHoldStart = 0;
bool railHold = false;
uint8_t *imageCopy;
size_t imageCopySize;
lv_image_dsc_t imageDsc;
uint32_t imageSeq = 0;
bool continuousMode = true, joystickHeld, railHeld;
float joystickX, joystickY, railZ;
lv_indev_t *driveTouch;
void stopDriveGesture(bool disarm = false);
void refreshMicUi();

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
    ui_layout::lockScroll(b);
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
    ui_layout::lockScroll(p);
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
    ui_layout::lockScroll(g);
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
void micToggle(lv_event_t *) {
    onboard_audio_set_mic(!onboard_audio_status().micEnabled);
    refreshMicUi(); // immediate local feedback, independent of the PC poll
}
void speakerTest(lv_event_t *) { onboard_audio_test(); }

void estop(lv_event_t *) {
    stopDriveGesture(true); command_execute("stop");
}
void rearm(lv_event_t *) { stopDriveGesture(); pc_drive_arm(); }

void saverEnter() {
    stopDriveGesture(true);
    onboard_audio_set_ptt(false);
}
void saverWake() {
    visitor_voice_cancel();
    lv_tabview_set_active(ui.tabview, TAB_MAIN, LV_ANIM_OFF);
    visitor_voice_request(VisitorVoice::Touch, ui_language() == UI_LANG_KO);
}

void tabVoice(lv_event_t *event) {
    if (lv_event_get_target(event) != ui.tabview || ui_screensaver_active()) return;
    const VisitorVoice voices[] = {VisitorVoice::Touch, VisitorVoice::Drive, VisitorVoice::Rail,
        VisitorVoice::Camera, VisitorVoice::Voice, VisitorVoice::Settings, VisitorVoice::Settings};
    const unsigned tab = lv_tabview_get_tab_active(ui.tabview);
    if (tab < TAB_COUNT) visitor_voice_request(voices[tab], ui_language() == UI_LANG_KO);
}

// ---- Drive tab ---------------------------------------------------------------------------------

constexpr float kStickTravel = 142.0f;
int shownStickPercent = -1;

void setState(lv_obj_t *obj, lv_state_t state, bool on) {
    if (on) lv_obj_add_state(obj, state);
    else lv_obj_remove_state(obj, state);
}
void disableRailArea(lv_obj_t *obj, bool disabled) {
    setState(obj, LV_STATE_DISABLED, disabled);
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        disableRailArea(lv_obj_get_child(obj, i), disabled);
}

void updateRailLabels() {
    if (ui.stepLabel) lv_label_set_text_fmt(ui.stepLabel, "Unit step: %d steps", unitStep);
    if (ui.railLabel) lv_label_set_text_fmt(ui.railLabel, pc_status().railPidMode ? "PID target: %d mm" : "Z step: %d steps", pc_status().railPidMode ? railTarget : unitStep);
    if (ui.pidButton) lv_label_set_text(lv_obj_get_child(ui.pidButton, 0), "Rail controls");
}

void pidToggle(lv_event_t *) {
    stopDriveGesture(true);
    lv_tabview_set_active(ui.tabview, TAB_RAIL, LV_ANIM_OFF);
}

void railAdjust(lv_event_t *e) {
    const int dir = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const auto pc = pc_status();
    if (pc.railPidMode) {
        railTarget += dir * 5;
        if (railTarget < pc.railMin) railTarget = pc.railMin;
        if (railTarget > pc.railMax) railTarget = pc.railMax;
        pc_rail_request("target", railTarget);
        railHold = true;
        railHoldStart = lv_tick_get();
    } else {
        pc_rail_request("step", dir * unitStep);
    }
    updateRailLabels();
}

void stepChange(lv_event_t *e) {
    unitStep += static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (unitStep < 1) unitStep = 1;
    if (unitStep > 2000) unitStep = 2000;
    updateRailLabels();
}

void stepPreset(lv_event_t *e) {
    unitStep = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    updateRailLabels();
}

void stepMove(lv_event_t *e) {
    // FW, BW, CW, CCW, X+, X-, Y+, Y-. One bounded batch per tap.
    static const int direction[8][2] = {{1,-1}, {-1,1}, {1,1}, {-1,-1}, {1,0}, {-1,0}, {0,1}, {0,-1}};
    const int move = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (move < 0 || move >= 8) return;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "b%d,%d", direction[move][0] * unitStep, direction[move][1] * unitStep);
    command_execute(cmd);
}

void refreshStickLevel() {
    if (!ui.joystickLevel || !ui.driveDeadzone) return;
    const float radius = std::fmin(1.0f, std::hypot(joystickX, joystickY));
    const float deadzone = lv_slider_get_value(ui.driveDeadzone) / 100.0f;
    const float distance = radius <= deadzone ? 0.0f : (radius - deadzone) / (1.0f - deadzone);
    const int rounded = static_cast<int>(std::lround(distance * distance * 100.0f));
    const int percent = radius <= deadzone ? 0 : (rounded > 0 ? rounded : 1);
    if (percent == shownStickPercent) return;
    shownStickPercent = percent;
    if (percent == 0) lv_label_set_text(ui.joystickLevel, "Dead zone / stopped");
    else lv_label_set_text_fmt(ui.joystickLevel, "Stick strength: %d %%", percent);
    lv_obj_set_style_text_color(ui.joystickLevel, percent ? kAccent : kMuted, 0);
}

void stopDriveGesture(bool disarm) {
    joystickHeld = railHeld = false;
    joystickX = joystickY = railZ = 0;
    driveTouch = nullptr;
    if (ui.joystickKnob) lv_obj_align(ui.joystickKnob, LV_ALIGN_CENTER, 0, 0);
    refreshStickLevel();
    if (disarm) pc_drive_disarm();
    else pc_drive_neutral();
}

void driveModeChange(lv_event_t *) {
    stopDriveGesture(true);
    continuousMode = !continuousMode;
    lv_label_set_text(lv_obj_get_child(ui.driveMode, 0), continuousMode ? "Mode: continuous" : "Mode: single step");
    for (lv_obj_t *obj : {ui.driveSteps, ui.driveStepSettings})
        lv_obj_update_flag(obj, LV_OBJ_FLAG_HIDDEN, continuousMode);
    for (lv_obj_t *obj : {ui.driveContinuous, ui.driveContinuousSettings})
        lv_obj_update_flag(obj, LV_OBJ_FLAG_HIDDEN, !continuousMode);
}

void joystickEvent(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) { stopDriveGesture(); return; }
    if (!continuousMode || (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING)) return;
    driveTouch = lv_indev_active();
    if (!driveTouch) return;
    lv_point_t point;
    lv_indev_get_point(driveTouch, &point);
    lv_area_t bounds;
    lv_obj_get_coords(ui.joystick, &bounds);
    float x = (point.x - (bounds.x1 + lv_obj_get_width(ui.joystick) / 2.0f)) / kStickTravel;
    float y = (point.y - (bounds.y1 + lv_obj_get_height(ui.joystick) / 2.0f)) / kStickTravel;
    const float radius = std::hypot(x, y);
    if (radius > 1) { x /= radius; y /= radius; }
    joystickX = x; joystickY = y;
    joystickHeld = true;
    lv_obj_align(ui.joystickKnob, LV_ALIGN_CENTER, static_cast<int>(x * kStickTravel), static_cast<int>(y * kStickTravel));
    refreshStickLevel();
}

void railHoldEvent(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) { stopDriveGesture(); return; }
    if (pc_status().railPidMode) return; // continuous signed steps belong to open loop only
    if (!continuousMode || (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING)) return;
    driveTouch = lv_indev_active();
    railHeld = true;
    railZ = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
}

void driveSpeedChange(lv_event_t *) {
    lv_label_set_text_fmt(ui.driveInterval, "Max speed: %d %%", static_cast<int>(lv_slider_get_value(ui.driveSpeed)));
}

void deadzoneChange(lv_event_t *) {
    stopDriveGesture();
    const int percent = lv_slider_get_value(ui.driveDeadzone);
    lv_label_set_text_fmt(ui.deadzoneValue, "Dead zone: %d %%", percent);
    const int diameter = static_cast<int>(2 * kStickTravel * percent / 100.0f);
    lv_obj_set_size(ui.joystickDeadzone, diameter, diameter);
    lv_obj_center(ui.joystickDeadzone);
}

void driveTouchTick(lv_timer_t *) {
    if (!continuousMode || (!joystickHeld && !railHeld)) return;
    const auto pc = pc_status();
    if (!driveTouch || lv_indev_get_state(driveTouch) != LV_INDEV_STATE_PRESSED ||
        lv_tabview_get_tab_active(ui.tabview) != TAB_DRIVE || pc.approvalPending ||
        !pc.reachable || !pc.board[PC_DRIVE].online || pc.sorting.running ||
        !ard_network_connected()) { stopDriveGesture(true); return; }
    // Send the raw stick once. The PC applies the radial curve before the
    // differential mix; its legacy per-axis deadzone is not applied again.
    pc_drive_joystick(joystickX, -joystickY, railZ, lv_slider_get_value(ui.driveSpeed), lv_slider_get_value(ui.driveDeadzone));
}

void driveTabChange(lv_event_t *e) {
    if (lv_event_get_target(e) == ui.tabview && lv_tabview_get_tab_active(ui.tabview) != TAB_DRIVE)
        stopDriveGesture(true);
}

lv_obj_t *driveArea(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    ui_layout::lockScroll(obj);
    return obj;
}

void buildDrive(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(tab, 12, 0);
    lv_obj_t *left = panel(tab, 564, ui_layout::kPageHeight);
    lv_obj_set_style_pad_all(left, 12, 0);
    label(left, "DriveMCU (via PC)", &ard_font_20, kAccent);
    ui.driveState = label(left, "---", &ard_font_16, kMuted);
    lv_obj_set_pos(ui.driveState, 0, 28);
    lv_obj_set_size(ui.driveState, 280, 22);
    ui.driveMode = button(left, "Mode: continuous", kAccent, driveModeChange, nullptr, 244, 44);
    lv_obj_set_pos(ui.driveMode, 296, 0);
    ui.driveControlState = label(left, "Touch the stick to move", &ard_font_16, kMuted);
    lv_obj_set_pos(ui.driveControlState, 0, 56);
    lv_obj_set_size(ui.driveControlState, 540, 36);
    ui.pos = label(left, "X 0  Y 0  Z 0", &ard_font_16);
    ui.env = label(left, "Env: ---", &ard_font_16);
    ui.tof = label(left, "ToF: ---", &ard_font_16);
    ui.gps = label(left, "GPS: off", &ard_font_16);
    int y = 102;
    for (lv_obj_t *item : {ui.pos, ui.env, ui.tof, ui.gps}) {
        lv_obj_set_pos(item, 0, y); y += 28;
        lv_obj_set_size(item, 540, 24);
        lv_label_set_long_mode(item, LV_LABEL_LONG_WRAP);
    }
    lv_obj_t *power = driveArea(left, 0, 220, 540, 48);
    const char *powerText[] = {"Enable", "Disable", "Rail controls", "HOME"};
    const char *powerCommand[] = {"e", "d", nullptr, "home"};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *b = button(power, powerText[i], i == 0 ? kOn : i == 1 ? kOff : kMuted,
                             i == 2 ? pidToggle : localCommand, const_cast<char *>(powerCommand[i]), 129, 48);
        lv_obj_set_pos(b, i * 137, 0);
        if (i == 2) ui.pidButton = b;
    }

    ui.driveStepSettings = driveArea(left, 0, 280, 540, 144);
    ui.stepLabel = label(ui.driveStepSettings, "", &ard_font_20, kAccent);
    for (int dir : {-1, 1}) {
        lv_obj_t *b = button(ui.driveStepSettings, dir < 0 ? "-1" : "+1", kMuted, stepChange,
                             reinterpret_cast<void *>(static_cast<intptr_t>(dir)), 76, 44);
        lv_obj_set_pos(b, dir < 0 ? 0 : 88, 34);
        lv_obj_add_event_cb(b, stepChange, LV_EVENT_LONG_PRESSED_REPEAT,
                            reinterpret_cast<void *>(static_cast<intptr_t>(dir)));
    }
    const int presets[] = {1, 10, 50, 100, 500, 1000, 2000};
    for (int i = 0; i < 7; ++i) {
        char caption[12]; snprintf(caption, sizeof(caption), "%d", presets[i]);
        lv_obj_t *b = button(ui.driveStepSettings, caption, kAccent, stepPreset,
                             reinterpret_cast<void *>(static_cast<intptr_t>(presets[i])), 76, 44);
        lv_obj_set_pos(b, i < 4 ? 184 + i * 88 : 272 + (i - 4) * 88, i < 4 ? 34 : 94);
    }
    ui.railLabel = label(ui.driveStepSettings, "", &ard_font_16, kMuted);
    lv_obj_set_pos(ui.railLabel, 0, 96);
    lv_obj_set_size(ui.railLabel, 260, 42);
    lv_label_set_long_mode(ui.railLabel, LV_LABEL_LONG_WRAP);

    ui.driveContinuousSettings = driveArea(left, 0, 280, 540, 144);
    for (int dir : {1, -1}) {
        lv_obj_t *b = button(ui.driveContinuousSettings, dir > 0 ? "Rail +" : "Rail -", kAccent,
                             railHoldEvent, reinterpret_cast<void *>(static_cast<intptr_t>(dir)), 130, 60);
        lv_obj_set_pos(b, dir > 0 ? 0 : 142, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_PRESS_LOCK);
        for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
            lv_obj_add_event_cb(b, railHoldEvent, code, reinterpret_cast<void *>(static_cast<intptr_t>(dir)));
    }
    lv_obj_t *railTitle = label(ui.driveContinuousSettings, "Rail: hold to move", &ard_font_16, kAccent);
    lv_obj_set_pos(railTitle, 0, 72);
    lv_obj_t *note = label(ui.driveContinuousSettings, "Smooth velocity control", &ard_font_16, kMuted);
    lv_obj_set_pos(note, 0, 108);
    ui.driveInterval = label(ui.driveContinuousSettings, "", &ard_font_16, kMuted);
    lv_obj_set_pos(ui.driveInterval, 300, 0);
    ui.driveSpeed = lv_slider_create(ui.driveContinuousSettings);
    lv_obj_set_pos(ui.driveSpeed, 308, 32);
    lv_obj_set_size(ui.driveSpeed, 216, 18);
    ui_layout::lockScroll(ui.driveSpeed);
    lv_slider_set_range(ui.driveSpeed, 10, 100);
    lv_slider_set_value(ui.driveSpeed, 100, LV_ANIM_OFF);
    lv_obj_add_event_cb(ui.driveSpeed, driveSpeedChange, LV_EVENT_VALUE_CHANGED, nullptr);
    ui.deadzoneValue = label(ui.driveContinuousSettings, "", &ard_font_16, kMuted);
    lv_obj_set_pos(ui.deadzoneValue, 300, 66);
    ui.driveDeadzone = lv_slider_create(ui.driveContinuousSettings);
    lv_obj_set_pos(ui.driveDeadzone, 308, 102);
    lv_obj_set_size(ui.driveDeadzone, 216, 18);
    ui_layout::lockScroll(ui.driveDeadzone);
    lv_slider_set_range(ui.driveDeadzone, 5, 30);
    lv_slider_set_value(ui.driveDeadzone, 20, LV_ANIM_OFF);
    lv_obj_add_event_cb(ui.driveDeadzone, deadzoneChange, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *right = panel(tab, 424, ui_layout::kPageHeight);
    lv_obj_set_style_pad_all(right, 12, 0);
    ui.driveSteps = driveArea(right, 0, 0, 400, 424);
    label(ui.driveSteps, "Quantitative move", &ard_font_20, kAccent);
    const char *moves[] = {"Disk FW", "Disk BW", "Rot CW", "Rot CCW", "X +", "X -", "Y +", "Y -"};
    for (int i = 0; i < 8; ++i) {
        lv_obj_t *b = button(ui.driveSteps, moves[i], kAccent, stepMove,
                             reinterpret_cast<void *>(static_cast<intptr_t>(i)), 194, 64);
        lv_obj_set_pos(b, (i % 2) * 206, 40 + (i / 2) * 76);
    }
    for (int dir : {1, -1}) {
        lv_obj_t *b = button(ui.driveSteps, dir > 0 ? "Rail +" : "Rail -", kAccent, railAdjust,
                             reinterpret_cast<void *>(static_cast<intptr_t>(dir)), 194, 64);
        lv_obj_set_pos(b, dir > 0 ? 0 : 206, 344);
    }

    ui.driveContinuous = driveArea(right, 0, 0, 400, 424);
    label(ui.driveContinuous, "Disk / rotation", &ard_font_20, kAccent);
    ui.joystick = driveArea(ui.driveContinuous, 20, 32, 360, 360);
    lv_obj_set_style_radius(ui.joystick, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui.joystick, lv_color_hex(0xE0EAF0), 0);
    lv_obj_set_style_bg_opa(ui.joystick, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui.joystick, 2, 0);
    lv_obj_set_style_border_color(ui.joystick, kAccent, 0);
    lv_obj_add_flag(ui.joystick, LV_OBJ_FLAG_PRESS_LOCK);
    for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
        lv_obj_add_event_cb(ui.joystick, joystickEvent, code, nullptr);
    for (int axis : {0, 1}) {
        lv_obj_t *guide = driveArea(ui.joystick, 0, 0, axis == 0 ? 284 : 2, axis == 0 ? 2 : 284);
        lv_obj_set_style_bg_color(guide, lv_color_hex(0xB7CCD9), 0);
        lv_obj_set_style_bg_opa(guide, LV_OPA_COVER, 0);
        lv_obj_remove_flag(guide, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(guide);
    }
    ui.joystickDeadzone = driveArea(ui.joystick, 0, 0, 56, 56);
    lv_obj_set_style_radius(ui.joystickDeadzone, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui.joystickDeadzone, kOn, 0);
    lv_obj_set_style_bg_opa(ui.joystickDeadzone, LV_OPA_20, 0);
    lv_obj_set_style_border_width(ui.joystickDeadzone, 2, 0);
    lv_obj_set_style_border_color(ui.joystickDeadzone, kOn, 0);
    lv_obj_remove_flag(ui.joystickDeadzone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(ui.joystickDeadzone);
    lv_obj_align(label(ui.joystick, "^", &ard_font_20, kAccent), LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_align(label(ui.joystick, "v", &ard_font_20, kAccent), LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_align(label(ui.joystick, "<", &ard_font_20, kAccent), LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_align(label(ui.joystick, ">", &ard_font_20, kAccent), LV_ALIGN_RIGHT_MID, -6, 0);
    ui.joystickKnob = driveArea(ui.joystick, 0, 0, 48, 48);
    lv_obj_set_style_radius(ui.joystickKnob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui.joystickKnob, kAccent, 0);
    lv_obj_set_style_bg_opa(ui.joystickKnob, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ui.joystickKnob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(ui.joystickKnob);
    ui.joystickLevel = label(ui.driveContinuous, "", &ard_font_16, kMuted);
    lv_obj_set_pos(ui.joystickLevel, 0, 398);
    lv_obj_set_size(ui.joystickLevel, 400, 24);
    lv_obj_set_style_text_align(ui.joystickLevel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(ui.driveSteps, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.driveStepSettings, LV_OBJ_FLAG_HIDDEN);
    updateRailLabels();
    driveSpeedChange(nullptr);
    deadzoneChange(nullptr);
}

// ---- Dedicated rail controls: selected control mode is separate from PID motor ON/OFF ----
bool requestRail(const char *op, int value = 0) {
    if (railRequestPending) return false;
    railRequestSequence = pc_rail_result().sequence;
    if (!pc_rail_request(op, value)) {
        lv_label_set_text(ui.railMessage, "Rail request unavailable");
        return false;
    }
    railRequestPending = true;
    railRequestStart = lv_tick_get();
    lv_label_set_text(ui.railMessage, "Waiting for PC / board ACK...");
    return true;
}

void railModeChanged(lv_event_t *) {
    const bool wantPid = lv_obj_has_state(ui.railMode, LV_STATE_CHECKED);
    requestRail(wantPid ? "pid" : "open_loop");
    // Selection follows the PC's saved mode, never optimistic local queue acceptance.
    setState(ui.railMode, LV_STATE_CHECKED, pc_status().railPidMode);
}
void railTargetChanged(lv_event_t *) {
    railTarget = lv_slider_get_value(ui.railTargetSlider);
    railDraft = true;
    lv_label_set_text_fmt(ui.railTargetText, "Target: %d mm", railTarget);
}
void railTargetNudge(lv_event_t *e) {
    const int dir = (int)(intptr_t)lv_event_get_user_data(e);
    lv_slider_set_value(ui.railTargetSlider, railTarget + dir, LV_ANIM_OFF);
    railTargetChanged(nullptr);
}
void railPreset(lv_event_t *e) {
    const auto pc = pc_status();
    const int values[] = {pc.railPlus, pc.railCenter, pc.railMinus, pc.railCapture};
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index > 3) return;
    lv_slider_set_value(ui.railTargetSlider, values[index], LV_ANIM_OFF);
    railTargetChanged(nullptr); // preset selection does not move the rail
}
void railMoveTarget(lv_event_t *) { requestRail("target", railTarget); }
void railStopTarget(lv_event_t *) { requestRail("stop"); }
void railStepMove(lv_event_t *e) {
    const int dir = (int)(intptr_t)lv_event_get_user_data(e);
    requestRail("step", dir * unitStep);
}
void railUnitChanged(lv_event_t *e) {
    unitStep += (int)(intptr_t)lv_event_get_user_data(e);
    if (unitStep < 1) unitStep = 1;
    if (unitStep > 2000) unitStep = 2000;
    updateRailLabels();
}

void buildRail(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(tab, 12, 0);
    lv_obj_t *left = panel(tab, 484, ui_layout::kPageHeight);
    lv_obj_set_style_pad_all(left, 12, 0);
    lv_obj_t *title = label(left, "Rail control", &ard_font_28, kAccent);
    lv_obj_set_pos(title, 0, 0);
    ui.railModeText = label(left, "Rail mode: waiting", &ard_font_20, kAccent);
    lv_obj_set_pos(ui.railModeText, 0, 44);
    ui.railMode = lv_switch_create(left);
    lv_obj_set_pos(ui.railMode, 356, 42); lv_obj_set_size(ui.railMode, 88, 42);
    lv_obj_add_event_cb(ui.railMode, railModeChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    ui.railLive = label(left, "ToF: ---", &ard_font_20);
    lv_obj_set_pos(ui.railLive, 0, 100); lv_obj_set_size(ui.railLive, 452, 150);
    lv_label_set_long_mode(ui.railLive, LV_LABEL_LONG_WRAP);
    ui.railGains = label(left, "Board P / I / D: ---", &ard_font_16, kMuted);
    lv_obj_set_pos(ui.railGains, 0, 260); lv_obj_set_width(ui.railGains, 452);
    ui.railMessage = label(left, "Select mode, then press MOVE", &ard_font_16, kMuted);
    lv_obj_set_pos(ui.railMessage, 0, 308); lv_obj_set_size(ui.railMessage, 452, 60);
    lv_label_set_long_mode(ui.railMessage, LV_LABEL_LONG_WRAP);
    ui.railStop = button(left, "STOP RAIL", kOff, railStopTarget, nullptr, 452, 48);
    lv_obj_set_pos(ui.railStop, 0, 372);
    lv_obj_t *right = panel(tab, 504, ui_layout::kPageHeight);
    lv_obj_set_style_pad_all(right, 12, 0);
    ui.railPidArea = driveArea(right, 0, 0, 480, 432);
    ui.railTargetText = label(ui.railPidArea, "Target: 120 mm", &ard_font_28, kAccent);
    ui.railTargetSlider = lv_slider_create(ui.railPidArea);
    lv_obj_set_pos(ui.railTargetSlider, 16, 60); lv_obj_set_size(ui.railTargetSlider, 448, 24);
    lv_slider_set_range(ui.railTargetSlider, 60, 130);
    lv_slider_set_value(ui.railTargetSlider, 120, LV_ANIM_OFF);
    lv_obj_add_event_cb(ui.railTargetSlider, railTargetChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    for (int dir : {-1, 1}) {
        lv_obj_t *b = button(ui.railPidArea, dir < 0 ? "-1 mm" : "+1 mm", kMuted,
            railTargetNudge, (void *)(intptr_t)dir, 230, 48);
        lv_obj_set_pos(b, dir < 0 ? 0 : 242, 104);
    }
    const char *presets[] = {"Metal/Paper", "Center", "Plastic/Glass", "Home"}; // Home: sequence origin ToF
    for (int i = 0; i < 4; ++i) {
        ui.railPresets[i] = button(ui.railPidArea, presets[i], kAccent, railPreset, (void *)(intptr_t)i, 230, 66);
        lv_obj_set_pos(ui.railPresets[i], (i % 2) * 242, 172 + (i / 2) * 80);
    }
    ui.railStart = button(ui.railPidArea, "MOVE TO TARGET", kOn, railMoveTarget, nullptr, 472, 60);
    lv_obj_set_pos(ui.railStart, 0, 352);
    ui.railOpenArea = driveArea(right, 0, 0, 480, 432);
    label(ui.railOpenArea, "Open loop / signed steps", &ard_font_20, kAccent);
    lv_obj_t *note = label(ui.railOpenArea, "ToF is display only in open loop", &ard_font_16, kMuted);
    lv_obj_set_pos(note, 0, 42);
    for (int dir : {-1, 1}) {
        lv_obj_t *unit = button(ui.railOpenArea, dir < 0 ? "Unit -10" : "Unit +10", kMuted,
            railUnitChanged, (void *)(intptr_t)(dir * 10), 230, 60);
        lv_obj_set_pos(unit, dir < 0 ? 0 : 242, 108);
        lv_obj_t *move = button(ui.railOpenArea, dir < 0 ? "Z - steps" : "Z + steps", kAccent,
            railStepMove, (void *)(intptr_t)dir, 230, 76);
        lv_obj_set_pos(move, dir < 0 ? 0 : 242, 208);
    }
}

void refreshRail(const PcStatus &pc) {
    const auto result = pc_rail_result();
    if (railRequestPending && result.sequence != railRequestSequence) {
        railRequestPending = false;
        if (result.code == 200) {
            railDraft = false;
            lv_label_set_text(ui.railMessage, "Accepted; check live motion status");
        } else lv_label_set_text_fmt(ui.railMessage, "Rail request failed: HTTP %d", result.code);
    }
    if (railRequestPending && lv_tick_elaps(railRequestStart) > 8000) {
        railRequestPending = false;
        lv_label_set_text(ui.railMessage, "Rail request timed out; check status");
    }
    const bool available = pc.reachable && pc.board[PC_DRIVE].online && pc.driveLive && pc.railConfigValid && !pc.sorting.running;
    setState(ui.railMode, LV_STATE_CHECKED, pc.railPidMode);
    setState(ui.railMode, LV_STATE_DISABLED, !available || railRequestPending);
    lv_label_set_text(ui.railModeText, !pc.railConfigValid ? "Rail mode: waiting" : pc.railPidMode ? "Rail mode: PID" : "Rail mode: open loop");
    lv_obj_update_flag(ui.railPidArea, LV_OBJ_FLAG_HIDDEN, !pc.railPidMode);
    lv_obj_update_flag(ui.railOpenArea, LV_OBJ_FLAG_HIDDEN, pc.railPidMode);
    disableRailArea(ui.railPidArea, !available || railRequestPending);
    disableRailArea(ui.railOpenArea, !available || railRequestPending || pc.driveBusy || pc.drivePid);
    setState(ui.railStart, LV_STATE_DISABLED, !available || railRequestPending || !pc.driveTofOk || (pc.driveBusy && !pc.drivePid));
    setState(ui.railStop, LV_STATE_DISABLED, !available || railRequestPending);
    if (pc.railConfigValid) {
        lv_slider_set_range(ui.railTargetSlider, pc.railMin, pc.railMax);
        if (!railDraft && !railRequestPending && pc.driveRailTarget > 0) railTarget = pc.driveRailTarget;
        lv_slider_set_value(ui.railTargetSlider, railTarget, LV_ANIM_OFF);
        lv_label_set_text_fmt(ui.railTargetText, "Target: %d mm", railTarget);
        const char *names[] = {"Metal/Paper", "Center", "Plastic/Glass", "Home"};
        const int values[] = {pc.railPlus, pc.railCenter, pc.railMinus, pc.railCapture};
        for (int i = 0; i < 4; ++i) {
            char text[64]; snprintf(text, sizeof(text), "%s\n%d mm", ui_tr(names[i]), values[i]);
            lv_label_set_text(lv_obj_get_child(ui.railPresets[i], 0), text);
        }
    }
    if (!available) lv_label_set_text(ui.railLive, "Rail state unavailable / sorting active");
    else if (!pc.driveTofOk) lv_label_set_text_fmt(ui.railLive, "ToF unavailable (%s)\nBoard target: %d mm\nPID: %s\nUnit: %d steps",
        pc.driveTofStatus, pc.driveRailTarget, pc.drivePid ? "ON" : "OFF", unitStep);
    else lv_label_set_text_fmt(ui.railLive, "ToF: %.1f mm (%s)\nBoard target: %d mm\nError: %.1f mm\nPID: %s / %s\nUnit: %d steps",
        pc.driveTof, pc.driveTofStatus, pc.driveRailTarget, pc.driveTof - pc.driveRailTarget,
        pc.drivePid ? "ON" : "OFF", pc.driveRailSettled ? ui_tr("settled") : ui_tr("moving / stopped"), unitStep);
    if (pc.railGainsValid) lv_label_set_text_fmt(ui.railGains, "Board P / I / D: %.3f / %.3f / %.3f", pc.railP, pc.railI, pc.railD);
    else lv_label_set_text(ui.railGains, "Board P / I / D: ---");
}

// ---- Cam / Sound tabs (boards that are still separate, through the PC) --------------------------

lv_obj_t *statusColumn(lv_obj_t *tab, const char *title, lv_obj_t **online, lv_obj_t **info) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(tab, 12, 0);
    lv_obj_t *left = panel(tab, 360, ui_layout::kPageHeight);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(left, 8, 0);
    label(left, title, &ard_font_28, kAccent);
    *online = label(left, "Offline", &ard_font_20, kOff);
    *info = label(left, "---", &ard_font_16);
    lv_label_set_long_mode(*info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(*info, 330);
    return grid(tab, ui_layout::kPageWidth - 372, ui_layout::kPageHeight);
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
    lv_obj_t *left = panel(tab, 348, ui_layout::kPageHeight);
    lv_obj_set_style_pad_all(left, 12, 0);
    label(left, "Camera", &ard_font_28, kAccent);
    ui.camOnline = label(left, "Offline", &ard_font_16, kOff);
    lv_obj_set_pos(ui.camOnline, 0, 40);
    lv_obj_set_size(ui.camOnline, 324, 40);
    lv_label_set_long_mode(ui.camOnline, LV_LABEL_LONG_WRAP);
    ui.camInfo = label(left, "---", &ard_font_16);
    lv_label_set_long_mode(ui.camInfo, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(ui.camInfo, 0, 84);
    lv_obj_set_size(ui.camInfo, 324, 212);
    lv_obj_t *g = grid(left, 324, 120);
    lv_obj_set_pos(g, 0, 304);
    lv_obj_set_style_pad_gap(g, 8, 0);
    button(g, "SHOT", kAccent, camCommand, (void *)"capture", 154, 52);
    button(g, "VIEW", kOn, viewImage, nullptr, 154, 52);
    button(g, "AI ANALYZE", kAccent, camCommand, (void *)"analyze", 154, 52);
    ui.camStream = button(g, "STREAM", kMuted, streamToggle, nullptr, 154, 52);

    lv_obj_t *view = panel(tab, ui_layout::kPageWidth - 360, ui_layout::kPageHeight);
    lv_obj_set_style_pad_all(view, 12, 0);
    lv_obj_set_style_bg_color(view, lv_color_hex(0x101418), 0);
    ui.camViewport = lv_obj_create(view);
    lv_obj_remove_style_all(ui.camViewport);
    lv_obj_set_size(ui.camViewport, LV_PCT(100), 392);
    ui_layout::lockScroll(ui.camViewport);
    ui.camImage = lv_image_create(ui.camViewport);
    ui_layout::lockScroll(ui.camImage);
    lv_obj_set_size(ui.camImage, 1, 1);
    // Use a destination-sized image object. Scaling an original-sized object
    // leaves its large layout bounds behind and can scroll or clip the page.
    lv_image_set_inner_align(ui.camImage, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_center(ui.camImage);
    ui.camImageNote = label(view, "New captures appear here automatically", &ard_font_16, lv_color_white());
    lv_obj_set_size(ui.camImageNote, LV_PCT(100), 24);
    lv_obj_set_style_text_align(ui.camImageNote, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ui.camImageNote, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui.camImageNote, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void refreshMicUi() {
    if (!ui.micToggleText || !ui.micState) return;
    const bool enabled = onboard_audio_status().micEnabled;
    lv_label_set_text(ui.micToggleText, enabled ? "Turn mic off" : "Turn mic on");
    lv_label_set_text(ui.micState, enabled ? "MIC ON" : "MIC OFF");
    lv_obj_set_style_text_color(ui.micState, enabled ? kOn : kOff, 0);
    lv_obj_set_style_bg_color(ui.micToggle, enabled ? kOn : kOff, 0);
    lv_obj_set_style_bg_color(ui.micToggle, kOn, LV_STATE_CHECKED);
    if (enabled) lv_obj_add_state(ui.micToggle, LV_STATE_CHECKED);
    else lv_obj_remove_state(ui.micToggle, LV_STATE_CHECKED);
    lv_obj_invalidate(ui.micToggle);
}

void buildSound(lv_obj_t *tab) {
    lv_obj_t *g = statusColumn(tab, "Onboard voice", &ui.soundOnline, &ui.soundInfo);
    button(g, "VAD", kOn, soundCommand, (void *)"vad", 175);
    button(g, "PTT", kAccent, soundCommand, (void *)"ptt", 175);
    ui.micToggle = button(g, "MIC ON", kMuted, micToggle, nullptr, 175);
    ui.micToggleText = lv_obj_get_child(ui.micToggle, 0);
    ui.micState = label(g, "", &ard_font_16, kMuted);
    lv_obj_set_width(ui.micState, 550);
    refreshMicUi();
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
    lv_obj_t *net = panel(tab, ui_layout::kPageWidth, ui_layout::kPageHeight);
    lv_obj_set_flex_flow(net, LV_FLEX_FLOW_COLUMN);
    label(net, "Network", &ard_font_28, kAccent);
    ui.netInfo = label(net, "---", &ard_font_16);
    lv_obj_set_width(ui.netInfo, ui_layout::kPageWidth - 28);
    lv_label_set_long_mode(ui.netInfo, LV_LABEL_LONG_WRAP);
    button(net, "Setup QR", kAccent, showSetupQr, nullptr, 200, 40);
    ui.sysInfo = label(net, "", &ard_font_16, kMuted);

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
    ui_layout::lockScroll(ui.approval);
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
    lv_obj_set_size(bar, LV_PCT(100), ui_layout::kBarHeight);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, kBar, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = label(bar, "ARD", &ard_font_28, lv_color_white());
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    ui.clock = label(bar, "--:--", &ard_font_28, lv_color_white());
    lv_obj_set_width(ui.clock, 104);
    lv_label_set_long_mode(ui.clock, LV_LABEL_LONG_CLIP);
    lv_obj_align(ui.clock, LV_ALIGN_LEFT_MID, 80, 0);
    ui.net = label(bar, "Wi-Fi --", &ard_font_16, lv_color_white());
    lv_obj_set_width(ui.net, 330);
    lv_label_set_long_mode(ui.net, LV_LABEL_LONG_CLIP);
    lv_obj_align(ui.net, LV_ALIGN_LEFT_MID, 190, -12);
    ui.pc = label(bar, "PC --", &ard_font_16, lv_color_white());
    lv_obj_set_width(ui.pc, 330);
    lv_label_set_long_mode(ui.pc, LV_LABEL_LONG_CLIP);
    lv_obj_align(ui.pc, LV_ALIGN_LEFT_MID, 190, 12);
    lv_obj_t *saver = button(bar, "Screensaver", kAccent, ui_screensaver_enter, nullptr, 128, 56);
    lv_obj_align(saver, LV_ALIGN_LEFT_MID, 526, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(saver, 0), &ard_font_16, 0);
    lv_obj_t *arm = button(bar, "RE-ARM", kMuted, rearm, nullptr, 120, 56);
    lv_obj_align(arm, LV_ALIGN_RIGHT_MID, -200, 0);
    lv_obj_t *stop = button(bar, "E-STOP", kOff, estop, nullptr, 180, 60);
    lv_obj_set_style_text_font(lv_obj_get_child(stop, 0), &ard_font_28, 0);
    lv_obj_align(stop, LV_ALIGN_RIGHT_MID, 0, 0);
}

// ---- periodic refresh ---------------------------------------------------------------------------

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
                lv_obj_update_layout(ui.camViewport);
                const int32_t availableW = lv_obj_get_content_width(ui.camViewport);
                const int32_t availableH = lv_obj_get_content_height(ui.camViewport);
                if (img.width && img.height && availableW > 0 && availableH > 0) {
                    int32_t width = availableW;
                    int32_t height = static_cast<int32_t>(static_cast<int64_t>(img.height) * width / img.width);
                    if (height > availableH) {
                        height = availableH;
                        width = static_cast<int32_t>(static_cast<int64_t>(img.width) * height / img.height);
                    }
                    lv_obj_set_size(ui.camImage, width > 0 ? width : 1, height > 0 ? height : 1);
                    // LVGL 9.2.2 computes STRETCH in set_src; commit the new
                    // destination bounds before it reads the object's size.
                    lv_obj_update_layout(ui.camImage);
                }
                lv_image_set_src(ui.camImage, &imageDsc);
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
    runtime_health_beat(HealthBeat::Ui);
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    if (local.tm_year >= 120) lv_label_set_text_fmt(ui.clock, "%02d:%02d", local.tm_hour, local.tm_min);
    else lv_label_set_text(ui.clock, "Sync...");
    refreshMicUi();

    char ip[16];
    ard_network_local_ip(ip, sizeof(ip));
    if (ard_network_connected()) lv_label_set_text_fmt(ui.net, "Wi-Fi %s  %d dBm", ip, ard_network_rssi());
    else lv_label_set_text(ui.net, ard_network_setup_ap_active() ? "Wi-Fi offline (setup AP on)" : "Wi-Fi offline");
    const PcStatus pc = pc_status();
    ui_screensaver_poll(pc.approvalPending || noticeVisible);
    // Announce confirmed state transitions once; never infer successful motion.
    static bool observed = false, wasReachable = false, wasSorting = false, wasArmed = false;
    const bool armedNow = pc_drive_is_armed();
    if (observed && !ui_screensaver_active()) {
        if (pc.reachable != wasReachable)
            visitor_voice_request(pc.reachable ? VisitorVoice::Online : VisitorVoice::Offline, ui_language() == UI_LANG_KO);
        if (armedNow && !wasArmed) visitor_voice_request(VisitorVoice::Rearm, ui_language() == UI_LANG_KO);
        if (pc.reachable && pc.sorting.valid) {
            if (pc.sorting.running && !wasSorting)
                visitor_voice_request(VisitorVoice::SortingStart, ui_language() == UI_LANG_KO);
            if (!pc.sorting.running && wasSorting && !strcmp(pc.sorting.phase, "done"))
                visitor_voice_request(VisitorVoice::SortingDone, ui_language() == UI_LANG_KO);
        }
    }
    observed = true; wasReachable = pc.reachable; wasArmed = armedNow;
    if (pc.reachable && pc.sorting.valid) wasSorting = pc.sorting.running;
    ui_settings_poll(lv_tabview_get_tab_active(ui.tabview) == TAB_SETTINGS && !pc.approvalPending && !ui_screensaver_active());
    ui_main_poll(pc, lv_tabview_get_tab_active(ui.tabview) == TAB_MAIN && !ui_screensaver_active());
    char server[65];
    ard_network_server(server, sizeof(server));
    lv_label_set_text_fmt(ui.pc, "PC %s %s", server, pc.reachable ? ui_tr("OK") : "--");

    switch (lv_tabview_get_tab_active(ui.tabview)) {
    case TAB_DRIVE: {
        const bool online = pc.reachable && pc.board[PC_DRIVE].online;
        if (online) lv_label_set_text_fmt(ui.pos, "PC estimate: X %ld   Y %ld   Z %ld",
            (long)pc.driveX, (long)pc.driveY, (long)pc.driveZ);
        else lv_label_set_text(ui.pos, "DriveMCU offline / PC unavailable");
        lv_label_set_text_fmt(ui.driveState, "DriveMCU %s  %s", pc.board[PC_DRIVE].ip, ui_tr(online ? "ONLINE" : "OFFLINE"));
        lv_label_set_text_fmt(ui.env, "Drive env: %.1f C  %.0f %%  %.1f hPa", pc.driveTemp, pc.driveHumidity, pc.drivePressure);
        if (pc.driveLive && pc.drivePid)
            lv_label_set_text_fmt(ui.tof, "Drive ToF: %.0f mm  %s  PID %s", pc.driveTof, pc.driveTofStatus,
                                  pc.driveRailSettled ? "settled" : "moving");
        else if (pc.driveLive)
            lv_label_set_text_fmt(ui.tof, "Drive ToF: %.0f mm  %s", pc.driveTof, pc.driveTofStatus);
        else
            lv_label_set_text_fmt(ui.tof, "Drive ToF: %.0f mm", pc.driveTof);
        lv_label_set_text_fmt(ui.gps, "Drive GPS: %.5f, %.5f", pc.driveLat, pc.driveLng);
        // The PID button and target follow the board, also after changes from the PC.
        if (railHold && lv_tick_elaps(railHoldStart) >= kRailHoldMs) railHold = false;
        if (!railHold) {
            pidOn = pc.drivePid;
            if (pc.driveLive && pc.driveRailTarget > 0) railTarget = pc.driveRailTarget;
        }
        updateRailLabels();
        const bool armed = pc_drive_is_armed();
        lv_label_set_text(ui.driveControlState, !continuousMode ? "One move per tap" :
                          !online ? "DriveMCU offline / PC unavailable" : pc.sorting.running ? "Sorting is running" :
                          pc.drivePid ? "Rail jog turns PID off" : armed ? "Ready: hold to move" : "Touch the stick to move");
        lv_obj_set_style_text_color(ui.driveControlState, continuousMode && (!online || pc.sorting.running) ? kOff : kOn, 0);
        break;
    }
    case TAB_RAIL:
        refreshRail(pc);
        break;
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
        const char *state = !audio.micEnabled ? "MIC OFF" : !audio.ready ? "ES8311 unavailable" :
                            !audio.selected ? "Select JC1060 voice on PC" : audio.recording ? "RECORDING" :
                            audio.uploading ? "UPLOADING" : audio.waiting ? "PROCESSING" :
                            audio.playing ? "SPEAKING" : "LISTENING";
        lv_label_set_text_fmt(ui.soundInfo, "%s  %s\nMode %s\nMic level %d / 32767\nVolume %d / 21%s\nClips %lu  errors %lu\n\nPTT: hold TALK\nVAD: automatic speech input\n\n%s",
            ui_tr(state), pc.soundStatus, ui_tr(audio.vad ? "VAD" : "PTT"), audio.peak, audio.volume,
            audio.muted ? ui_tr(" (muted)") : "",
            (unsigned long)audio.uploadedClips, (unsigned long)audio.uploadErrors, audio.error);
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

} // namespace

void ui_notice(const char *text) {
    static lv_obj_t *notice, *noticeText;
    if (!bsp_lvgl_lock(1000)) return;
    noticeVisible = text != nullptr;
    if (noticeVisible) ui_screensaver_poll(true);
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
    ui_layout::page(screen, 0);
    buildBar(screen);
    ui_screensaver_init(saverEnter, saverWake);
    ui.tabview = lv_tabview_create(screen);
    lv_tabview_set_tab_bar_size(ui.tabview, ui_layout::kTabBarHeight);
    lv_obj_set_size(ui.tabview, LCD_H_RES, LCD_V_RES - ui_layout::kBarHeight);
    ui_layout::tabview(ui.tabview);
    lv_obj_align(ui.tabview, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(lv_tabview_get_tab_bar(ui.tabview), &ard_font_20, 0);
    // Main (sorting sequence) is first, so it is the screen shown after boot.
    ui_main_create(lv_tabview_add_tab(ui.tabview, "Main"));
    buildDrive(lv_tabview_add_tab(ui.tabview, "Drive"));
    buildRail(lv_tabview_add_tab(ui.tabview, "Rail"));
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
        ui_layout::page(page, i == TAB_SETTINGS ? 0 : ui_layout::kPagePadding);
        lv_obj_set_style_bg_color(page, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    }
    buildApproval();
    lv_obj_add_event_cb(ui.tabview, driveTabChange, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(ui.tabview, tabVoice, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_timer_create(driveTouchTick, 50, nullptr);
    lv_timer_create(refresh, 250, nullptr);
    bsp_lvgl_unlock();
    return ESP_OK;
}
