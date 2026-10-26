#include "ui_main.h"
#include "ui_text.h"
#include "ui_layout.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "esp_heap_caps.h"
#include "esp_timer.h"

#define DEG "\xC2\xB0"

namespace {

const lv_color_t kText = lv_color_hex(0x1F2A36), kAccent = lv_color_hex(0x0B6E99), kOn = lv_color_hex(0x1E9E50),
                 kOff = lv_color_hex(0xB3261E), kMuted = lv_color_hex(0x5F6B76), kPanel = lv_color_hex(0xF2F4F6),
                 kTrack = lv_color_hex(0xC9D1D8), kTarget = lv_color_hex(0xE07B00);

struct Step { const char *title, *detail; };
constexpr int kStepCount = 6;
const Step kSteps[kStepCount] = {{"1  HOME", "Level, rail ToF"}, {"2  CAPTURE", "New camera image"},
                                 {"3  AI", "Material class"}, {"4  RAIL", "Move to the bin"},
                                 {"5  TILT", "Fwd/back to drop"}, {"6  RETURN", "Back to start"}};

// Sorting uses the rail and a forward/backward tray tilt only (no disk turn). The PC's
// destination table (rail config "destinations") says which material goes to which bin.
constexpr int kCategoryCount = 5;
const char *const kCategories[kCategoryCount] = {"Metal", "Glass", "Others", "Paper", "Plastic"};
// Badge colors; the bin cells use the former bin colors (Glass and Others share one bin).
const uint32_t kCategoryColors[kCategoryCount] = {0x6D7A86, 0x00897B, 0x7E57C2, 0x8D6E63, 0x1565C0};
const uint32_t kBinColors[kCategoryCount] = {0x6D7A86, 0x00897B, 0x00897B, 0x8D6E63, 0x1565C0};
// PC default table (core/rail_control.py DEFAULT_DESTINATIONS) until the PC sends its own.
const int8_t kDefaultRail[kCategoryCount] = {1, -1, -1, 1, -1}, kDefaultTilt[kCategoryCount] = {1, 1, 1, -1, -1};
// Four bins: right column forward tilt, left column backward; top row rail +, bottom row rail -.
struct Cell { int8_t tilt, rail; };
const Cell kCells[4] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

int binFor(const char *material) {
    for (int i = 0; i < kCategoryCount; ++i)
        if (!strcmp(material, kCategories[i])) return i;
    return -1;
}

int cellFor(float roll, float rail) {
    if (roll == 0 || rail == 0) return -1; // rail center has no bin cell
    for (int i = 0; i < 4; ++i)
        if (kCells[i].tilt == (roll > 0 ? 1 : -1) && kCells[i].rail == (rail > 0 ? 1 : -1)) return i;
    return -1;
}

// Disk (top view) and tray/rail (side view) geometry in panel content coordinates.
constexpr int kCx = 196, kCy = 164, kR = 100;
constexpr int kPivotX = 111, kPivotY = 92, kTrayHalf = 85;
constexpr int kRailX = 11, kRailY = 238, kRailW = 200;
constexpr float kRailRange = 20; // mm shown on each side of the start position
// Captured-image thumbnail in the info panel (10:7, like the 640x448 frame the PC sends).
constexpr int kThumbW = 128, kThumbH = 90;

struct View {
    lv_obj_t *card[kStepCount], *cardTitle[kStepCount], *cardDetail[kStepCount], *progress;
    lv_obj_t *target, *binCell[4], *binText[4];
    char binNames[4][48];         // categories shown in each bin cell
    uint32_t binColor[4];
    lv_obj_t *tray, *tiltLabel, *railMarker, *railTarget, *railLabel;
    lv_obj_t *state, *object, *badge, *badgeText, *bin, *activity, *updated, *devices, *start, *stop;
    lv_obj_t *thumb, *thumbNote;
    uint16_t *thumbPixels;        // kThumbW x kThumbH RGB565 in PSRAM (about 23 KB)
    lv_image_dsc_t thumbDsc;
    uint32_t thumbSeq;            // last camera frame shown as the thumbnail
    uint32_t resultSeen;          // last sorting request answer shown
    uint32_t noticeUntil;         // that answer replaces the activity line until then (ms)
    char notice[192];
    lv_point_precise_t trayPts[2];
    float roll, rail;               // displayed (animated) values
    int drawnRoll = INT32_MIN, drawnRail = INT32_MIN;
    PcSorting sorting;
    bool live, visible;
    int homeMm = 120;               // sequence origin: tray level, rail ToF (PC rail config home_mm)
} v;

lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y, const lv_font_t *font = &ard_font_20,
               lv_color_t color = kText) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, value);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color, int radius = 12) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    ui_layout::lockScroll(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, const char *title) {
    lv_obj_t *p = box(parent, x, y, w, h, kPanel);
    lv_obj_set_style_pad_all(p, 14, 0);
    lv_obj_t *heading = text(p, title, 0, 0, &ard_font_20, kAccent);
    lv_obj_set_width(heading, w - 28);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_WRAP);
    return p;
}

lv_obj_t *line(lv_obj_t *parent, lv_point_precise_t *points, int width, lv_color_t color) {
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_set_pos(l, 0, 0);
    lv_line_set_points(l, points, 2);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

int railToX(float mm) {
    if (mm < -kRailRange) mm = -kRailRange;
    if (mm > kRailRange) mm = kRailRange;
    return kRailX + (int)lroundf((mm + kRailRange) / (2 * kRailRange) * kRailW);
}

void buildSteps(lv_obj_t *page) {
    for (int i = 0; i < kStepCount; ++i) {
        v.card[i] = box(page, i * 168, 0, 160, 72, kPanel, 10);
        lv_obj_set_style_pad_all(v.card[i], 8, 0);
        v.cardTitle[i] = text(v.card[i], kSteps[i].title, 0, 0);
        v.cardDetail[i] = text(v.card[i], kSteps[i].detail, 0, 28, &ard_font_16, kMuted);
        for (lv_obj_t *label : {v.cardTitle[i], v.cardDetail[i]}) {
            lv_obj_set_width(label, 144);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        }
    }
    v.progress = lv_bar_create(page);
    lv_obj_set_pos(v.progress, 0, 82);
    lv_obj_set_size(v.progress, 1000, 12);
    lv_bar_set_range(v.progress, 0, 100);
    lv_obj_set_style_bg_color(v.progress, kTrack, LV_PART_MAIN);
    lv_obj_set_style_bg_color(v.progress, kAccent, LV_PART_INDICATOR);
}

void buildDisk(lv_obj_t *page) {
    lv_obj_t *p = panel(page, 0, 104, 420, 340, "Drop position (top view)");
    lv_obj_t *disk = box(p, kCx - kR, kCy - kR, 2 * kR, 2 * kR, lv_color_white(), LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(disk, 4, 0);
    lv_obj_set_style_border_color(disk, kTrack, 0);
    for (int i = 0; i < 4; ++i) {
        const bool right = kCells[i].tilt > 0, upper = kCells[i].rail > 0;
        v.binCell[i] = box(p, right ? kCx + kR + 12 : kCx - kR - 92, upper ? kCy - 62 : kCy + 4, 80, 58,
                           lv_color_white(), 8);
        lv_obj_set_style_border_width(v.binCell[i], 2, 0);
        lv_obj_set_style_border_color(v.binCell[i], kTrack, 0);
        v.binText[i] = text(v.binCell[i], "-", 0, 0, &ard_font_16, kText);
        lv_label_set_long_mode(v.binText[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_width(v.binText[i], 72);
        lv_obj_set_style_text_align(v.binText[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(v.binText[i]);
    }
    text(p, "Forward", kCx + kR + 20, kCy + 68, &ard_font_16, kMuted);
    text(p, "Backward", kCx - kR - 92, kCy + 68, &ard_font_16, kMuted);
    lv_obj_t *note = text(p, "Right: forward tilt, left: backward. Top: rail +, bottom: rail -", 0, 274, &ard_font_16, kMuted);
    lv_obj_set_size(note, 392, 38);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    v.target = box(p, 0, 0, 18, 18, kTarget, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(v.target, LV_OBJ_FLAG_HIDDEN);
    box(p, kCx - 9, kCy - 9, 18, 18, kText, LV_RADIUS_CIRCLE);
}

void buildTrayAndRail(lv_obj_t *page) {
    lv_obj_t *p = panel(page, 432, 104, 250, 340, "Tray tilt (side)");
    static lv_point_precise_t level[2] = {{kPivotX - 100, kPivotY}, {kPivotX + 100, kPivotY}};
    line(p, level, 2, kTrack);
    v.trayPts[0] = {kPivotX - kTrayHalf, kPivotY};
    v.trayPts[1] = {kPivotX + kTrayHalf, kPivotY};
    v.tray = line(p, v.trayPts, 10, kAccent);
    box(p, kPivotX - 7, kPivotY - 7, 14, 14, kText, LV_RADIUS_CIRCLE);
    v.tiltLabel = text(p, "", 0, 158); // below the tray tilted 45 deg

    text(p, "Rail position", 0, 190, &ard_font_20, kAccent);
    box(p, kRailX, kRailY, kRailW, 8, kTrack, 4);
    box(p, railToX(0) - 1, kRailY - 8, 2, 24, kMuted, 0);
    v.railTarget = box(p, 0, kRailY - 10, 4, 28, kTarget, 2);
    lv_obj_add_flag(v.railTarget, LV_OBJ_FLAG_HIDDEN);
    v.railMarker = box(p, 0, kRailY - 10, 16, 28, kAccent, 4);
    text(p, "-20", 0, kRailY + 20, &ard_font_16, kMuted);
    text(p, "+20 mm", 162, kRailY + 20, &ard_font_16, kMuted);
    v.railLabel = text(p, "", 0, 284);
}

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

void showNotice(const char *message) {
    strlcpy(v.notice, message, sizeof(v.notice));
    v.noticeUntil = nowMs() + 5000;
    lv_label_set_text(v.activity, v.notice);
}

// Same request as Start Sorting on the PC dashboard; gear ratios come from the PC settings.
void startSorting(lv_event_t *) {
    showNotice(ui_tr(pc_sorting_request(true) ? "Asking the PC to start sorting..." : "PC link busy. Try again."));
}

void stopSorting(lv_event_t *) {
    const bool running = pc_status().sorting.running;
    const bool queued = running ? pc_sorting_request(false) : pc_sorting_reset();
    showNotice(ui_tr(!queued ? "PC link busy. Use E-STOP." : running ? "Asking the PC to stop sorting..." : "Resetting camera and tray..."));
}

lv_obj_t *actionButton(lv_obj_t *parent, const char *caption, int x, int w, lv_color_t color, lv_event_cb_t cb) {
    lv_obj_t *b = lv_button_create(parent);
    ui_layout::lockScroll(b);
    lv_obj_set_pos(b, x, 260);
    lv_obj_set_size(b, w, 50);
    lv_obj_set_style_bg_color(b, color, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_center(text(b, caption, 0, 0, &ard_font_20, lv_color_white()));
    return b;
}

void buildInfo(lv_obj_t *page) {
    // Fixed rows so the buttons always stay at the bottom of the panel.
    lv_obj_t *p = panel(page, 694, 104, 306, 340, "Sorting sequence");
    v.updated = text(p, "", 178, 3, &ard_font_16, kMuted); // PC update time, right of the heading
    lv_obj_set_width(v.updated, 100);
    lv_obj_set_style_text_align(v.updated, LV_TEXT_ALIGN_RIGHT, 0);
    v.state = text(p, "", 0, 26, &ard_font_28);
    v.object = text(p, "", 0, 62);
    lv_label_set_long_mode(v.object, LV_LABEL_LONG_DOT);
    lv_obj_set_size(v.object, 278, 24);
    // Last captured image on the left, material and bin on the right.
    lv_obj_t *frame = box(p, 0, 90, kThumbW, kThumbH, kText, 6);
    v.thumb = lv_image_create(frame);
    lv_obj_add_flag(v.thumb, LV_OBJ_FLAG_HIDDEN);
    v.thumbNote = text(frame, "No image", 0, 0, &ard_font_16, lv_color_white());
    lv_label_set_long_mode(v.thumbNote, LV_LABEL_LONG_DOT);
    lv_obj_set_width(v.thumbNote, kThumbW - 8);
    lv_obj_set_style_text_align(v.thumbNote, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(v.thumbNote);
    v.badge = box(p, kThumbW + 10, 90, LV_SIZE_CONTENT, LV_SIZE_CONTENT, kMuted, 8);
    lv_obj_set_style_pad_hor(v.badge, 12, 0);
    lv_obj_set_style_pad_ver(v.badge, 4, 0);
    v.badgeText = text(v.badge, "-", 0, 0, &ard_font_20, lv_color_white());
    v.bin = text(p, "", kThumbW + 10, 124, &ard_font_16, kText);
    lv_label_set_long_mode(v.bin, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(v.bin, 278 - kThumbW - 10, 58);
    v.activity = text(p, "", 0, 186, &ard_font_16, kText);
    lv_label_set_long_mode(v.activity, LV_LABEL_LONG_DOT);
    lv_obj_set_size(v.activity, 278, 42);
    v.devices = text(p, "", 0, 232, &ard_font_16, kMuted);
    v.start = actionButton(p, "START", 0, 148, kOn, startSorting);
    v.stop = actionButton(p, "RESET", 156, 122, kOff, stopSorting);
}

// Plain-language answer to the latest start/stop request (HTTP status from the PC).
void describeResult(const PcRequestResult &r, char *out, size_t size) {
    const char *text;
    if (r.reset) {
        text = r.code == 200 ? "Resetting camera and tray..." : r.code == 409 ? "Stop the current task before reset." : nullptr;
    } else if (r.start) {
        text = r.code == 200 ? "Sorting started." : r.code == 409 ? "Sorting is already running."
             : r.code == 503 ? "DriveMCU or camera is offline." : r.code == 400 ? "Check the gear ratios in the PC settings."
             : nullptr;
    } else {
        text = r.code == 200 ? "Stop requested. Motors are halted." : r.code == 409 ? "No sorting is running." : nullptr;
    }
    if (text) strlcpy(out, ui_tr(text), size);
    else snprintf(out, size, ui_tr("The PC did not accept the request (%d)."), r.code);
}

bool phase(const char *name) { return !strcmp(v.sorting.phase, name); }

void describeActivity(char *out, size_t size) {
    if (phase("reset")) { strlcpy(out, ui_tr("Resetting camera and tray..."), size); return; }
    const PcSorting &s = v.sorting;
    const int b = binFor(s.material);
    if (!v.live) snprintf(out, size, ui_tr("Waiting for the PC sorting status."));
    else if (!strcmp(s.step, "error")) snprintf(out, size, ui_tr("Sequence stopped. Motors were halted; see the PC for details."));
    else if (!strcmp(s.step, "complete"))
        snprintf(out, size, ui_tr("Sorted into %s and returned to the start position."),
                 ui_tr(b >= 0 ? kCategories[b] : "the bin"));
    else if (!s.running) snprintf(out, size, ui_tr("Ready. Start sorting here or on the PC."));
    else if (phase("init")) snprintf(out, size, ui_tr("Returning to origin: tray level, rail ToF %d mm..."), v.homeMm);
    else if (phase("capture")) snprintf(out, size, ui_tr("Capturing a new camera image..."));
    else if (phase("analyze")) snprintf(out, size, ui_tr("AI is classifying the material..."));
    else if (phase("rail_out")) snprintf(out, size, ui_tr("Moving rail to %+.0f mm..."), s.targetRail);
    else if (phase("roll_out"))
        snprintf(out, size, ui_tr(s.dumpRoll < 0 ? "Tilting tray backward %.0f" DEG " to dump..."
                                                 : "Tilting tray forward %.0f" DEG " to dump..."), fabsf(s.dumpRoll));
    else if (phase("settle")) snprintf(out, size, ui_tr("Tray tilted. Waiting for the item to drop..."));
    else if (phase("roll_back")) snprintf(out, size, ui_tr("Leveling the tray..."));
    else if (phase("rail_back")) snprintf(out, size, ui_tr("Returning rail to the start position..."));
    else snprintf(out, size, ui_tr("Checking DriveMCU and camera..."));
}

void updateSteps() {
    const PcSorting &s = v.sorting;
    const bool complete = !strcmp(s.step, "complete"), error = !strcmp(s.step, "error");
    for (int i = 0; i < kStepCount; ++i) {
        const int n = i + 1;
        // Done green, current blue, failed red, pending grey.
        lv_color_t bg = kPanel;
        bool filled = true;
        if (v.live && (complete || n < s.stepNum)) bg = kOn;
        else if (v.live && n == s.stepNum && error) bg = kOff;
        else if (v.live && n == s.stepNum && s.running) bg = kAccent;
        else filled = false;
        lv_obj_set_style_bg_color(v.card[i], bg, 0);
        lv_obj_set_style_text_color(v.cardTitle[i], filled ? lv_color_white() : kText, 0);
        lv_obj_set_style_text_color(v.cardDetail[i], filled ? lv_color_white() : kMuted, 0);
    }
    lv_bar_set_value(v.progress, v.live ? s.progress : 0, LV_ANIM_ON);
}

void updateInfo(const PcStatus &pc) {
    const PcSorting &s = v.sorting;
    const bool complete = !strcmp(s.step, "complete"), error = !strcmp(s.step, "error");
    if (!v.live) {
        lv_label_set_text(v.state, "PC OFFLINE");
        lv_obj_set_style_text_color(v.state, kOff, 0);
    } else if (s.running) {
        lv_label_set_text_fmt(v.state, "RUNNING  %d%%", s.progress);
        lv_obj_set_style_text_color(v.state, kAccent, 0);
    } else {
        lv_label_set_text(v.state, error ? "STOPPED" : complete ? "COMPLETE" : "READY");
        lv_obj_set_style_text_color(v.state, error ? kOff : complete ? kOn : kMuted, 0);
    }
    lv_label_set_text_fmt(v.object, "Object: %s", v.live && s.object[0] ? s.object : "-");
    const int b = v.live ? binFor(s.material) : -1;
    // The PC publishes the chosen rail side and signed tilt (dump_roll) after the AI result.
    const bool placed = b >= 0 && s.dumpRoll != 0;
    const int cell = placed ? cellFor(s.dumpRoll, s.targetRail) : -1;
    if (b >= 0) {
        lv_label_set_text(v.badgeText, s.material);
        lv_obj_set_style_bg_color(v.badge, lv_color_hex(kCategoryColors[b]), 0);
        lv_obj_remove_flag(v.badge, LV_OBJ_FLAG_HIDDEN);
        if (placed)
            lv_label_set_text_fmt(v.bin, ui_tr(s.dumpRoll > 0 ? "Bin: rail %+.0f mm, forward tilt" : "Bin: rail %+.0f mm, backward tilt"),
                                  s.targetRail);
        else
            lv_label_set_text(v.bin, "Bin: -");
    } else {
        lv_obj_add_flag(v.badge, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(v.bin, "Bin: -");
    }
    for (int i = 0; i < 4; ++i) {
        const bool chosen = i == cell;
        lv_obj_set_style_bg_color(v.binCell[i], chosen ? lv_color_hex(v.binColor[i]) : lv_color_white(), 0);
        lv_obj_set_style_border_color(v.binCell[i], chosen ? kTarget : kTrack, 0);
        lv_obj_set_style_border_width(v.binCell[i], chosen ? 4 : 2, 0);
        lv_obj_set_style_text_color(v.binText[i], chosen ? lv_color_white() : kText, 0);
    }
    const PcRequestResult result = pc_sorting_result();
    if (result.sequence != v.resultSeen) {
        v.resultSeen = result.sequence;
        char message[192];
        describeResult(result, message, sizeof(message));
        showNotice(message);
    }
    if ((int32_t)(nowMs() - v.noticeUntil) >= 0) {
        char activity[192]; // Korean text is three bytes per character
        describeActivity(activity, sizeof(activity));
        lv_label_set_text(v.activity, activity);
    }
    // Start only when the PC answers and nothing runs; stop whenever a sequence runs.
    if (v.live && pc.reachable && !s.running && !s.needsReset) lv_obj_remove_state(v.start, LV_STATE_DISABLED);
    else lv_obj_add_state(v.start, LV_STATE_DISABLED);
    if (v.live && pc.reachable) lv_obj_remove_state(v.stop, LV_STATE_DISABLED);
    else lv_obj_add_state(v.stop, LV_STATE_DISABLED);
    lv_label_set_text(lv_obj_get_child(v.stop, 0), s.running ? "STOP" : "RESET");
    lv_label_set_text(v.updated, v.live && s.updated[0] ? s.updated : "--:--:--");
    lv_label_set_text_fmt(v.devices, "DriveMCU %s   Camera %s",
                          ui_tr(pc.reachable && pc.board[PC_DRIVE].online ? "online" : "offline"),
                          ui_tr(pc.reachable && pc.board[PC_CAM].online ? "online" : "offline"));
}

// Shows the camera frame the Cam tab already downloaded (no extra transfer), shrunk with
// nearest-neighbour sampling into a small PSRAM buffer so LVGL never scales the full frame.
void updateThumb() {
    PcImage img;
    if (!pc_image_lock(&img, 5)) return;
    if (img.sequence != v.thumbSeq) {
        v.thumbSeq = img.sequence;
        if (!v.thumbPixels)
            v.thumbPixels = (uint16_t *)heap_caps_malloc(kThumbW * kThumbH * 2, MALLOC_CAP_SPIRAM);
        if (img.pixels && img.width && img.height && v.thumbPixels) {
            int w = kThumbW, h = img.height * kThumbW / img.width;
            if (h > kThumbH) {
                h = kThumbH;
                w = img.width * kThumbH / img.height;
            }
            if (w < 1) w = 1;
            if (h < 1) h = 1;
            const uint16_t *src = (const uint16_t *)img.pixels;
            for (int y = 0; y < h; ++y) {
                const uint16_t *row = src + (size_t)((2 * y + 1) * img.height / (2 * h)) * img.stride;
                for (int x = 0; x < w; ++x) v.thumbPixels[y * w + x] = row[(2 * x + 1) * img.width / (2 * w)];
            }
            v.thumbDsc = {};
            v.thumbDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            v.thumbDsc.header.cf = LV_COLOR_FORMAT_RGB565;
            v.thumbDsc.header.w = w;
            v.thumbDsc.header.h = h;
            v.thumbDsc.header.stride = w * 2;
            v.thumbDsc.data = (const uint8_t *)v.thumbPixels;
            v.thumbDsc.data_size = w * h * 2;
            lv_obj_set_size(v.thumb, w, h);
            lv_image_set_src(v.thumb, &v.thumbDsc);
            lv_obj_center(v.thumb);
            lv_obj_invalidate(v.thumb); // same buffer, new pixels
            lv_obj_remove_flag(v.thumb, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(v.thumbNote, LV_OBJ_FLAG_HIDDEN);
        } else if (!img.pixels) {
            // Keep the last good picture; show the error only when there is none.
            if (lv_obj_has_flag(v.thumb, LV_OBJ_FLAG_HIDDEN)) lv_label_set_text(v.thumbNote, img.error[0] ? img.error : "No image");
        }
    }
    pc_image_unlock();
}

// Moves the displayed value toward the goal by at most `step` per frame.
float approach(float value, float goal, float step) {
    if (fabsf(goal - value) <= step) return goal;
    return value + (goal > value ? step : -step);
}

void animate(lv_timer_t *) {
    if (!v.visible) return;
    const PcSorting &s = v.sorting;
    // The PC confirms each move after it completes; animate the axis named by phase meanwhile.
    // No disk turn: only the rail and the signed tray tilt (+ forward, - backward) move.
    float rollGoal = s.currentRoll, railGoal = s.currentRail;
    if (v.live && s.running) {
        if (phase("roll_out")) rollGoal = s.dumpRoll;
        else if (phase("roll_back")) rollGoal = 0;
        if (phase("rail_out")) railGoal = s.targetRail;
        else if (phase("rail_back") || phase("init")) railGoal = 0;
        if (phase("init")) rollGoal = 0; // step 1 returns the tray to the origin
    }
    if (!v.live) rollGoal = railGoal = 0;
    // About 30 deg/s tilt and 10 mm/s rail at the 40 ms frame rate.
    v.roll = approach(v.roll, rollGoal, 1.2f);
    v.rail = approach(v.rail, railGoal, 0.4f);

    const int roll10 = (int)lroundf(v.roll * 10), rail10 = (int)lroundf(v.rail * 10);
    if (roll10 != v.drawnRoll) {
        v.drawnRoll = roll10;
        const float a = v.roll * (float)M_PI / 180;
        const int32_t dx = lroundf(kTrayHalf * cosf(a)), dy = lroundf(kTrayHalf * sinf(a));
        v.trayPts[0] = {kPivotX - dx, kPivotY - dy};
        v.trayPts[1] = {kPivotX + dx, kPivotY + dy};
        lv_line_set_points(v.tray, v.trayPts, 2);
        lv_label_set_text_fmt(v.tiltLabel, "%+.1f" DEG "  (%s %.0f" DEG ")", v.roll,
                              ui_tr(s.dumpRoll < 0 ? "backward" : "forward"), s.dumpRoll != 0 ? fabsf(s.dumpRoll) : 45.f);
    }
    if (rail10 != v.drawnRail) {
        v.drawnRail = rail10;
        lv_obj_set_x(v.railMarker, railToX(v.rail) - 8);
        lv_label_set_text_fmt(v.railLabel, "%+.1f mm", v.rail);
    }
}

} // namespace

void ui_main_create(lv_obj_t *page) {
    ui_layout::page(page);
    buildSteps(page);
    buildDisk(page);
    buildTrayAndRail(page);
    buildInfo(page);
    PcStatus empty = {};
    ui_main_poll(empty, true);
    animate(nullptr);
    lv_timer_create(animate, 40, nullptr);
}

// Bin cell labels from the PC's destination table (defaults until the PC sends one).
void updateBinNames(const PcStatus &pc) {
    for (int i = 0; i < 4; ++i) {
        char names[48] = "";
        uint32_t color = 0x9AA5AF;
        for (int c = 0; c < kCategoryCount; ++c) {
            const int rail = pc.destValid ? pc.destRail[c] : kDefaultRail[c];
            const int tilt = pc.destValid ? pc.destTilt[c] : kDefaultTilt[c];
            if (rail != kCells[i].rail || tilt != kCells[i].tilt) continue;
            if (!names[0]) color = kBinColors[c];
            else strlcat(names, ", ", sizeof(names));
            strlcat(names, ui_tr(kCategories[c]), sizeof(names));
        }
        if (!names[0]) strlcpy(names, "-", sizeof(names));
        v.binColor[i] = color;
        if (strcmp(names, v.binNames[i])) {
            strlcpy(v.binNames[i], names, sizeof(v.binNames[i]));
            lv_label_set_text(v.binText[i], names);
        }
    }
}

void ui_main_poll(const PcStatus &pc, bool visible) {
    v.visible = visible;
    if (!visible) return;
    updateBinNames(pc);
    if (pc.railConfigValid && pc.railCapture != v.homeMm) {
        v.homeMm = pc.railCapture;
        lv_label_set_text_fmt(v.cardDetail[0], ui_tr("Level, rail %d mm"), v.homeMm);
    }
    v.drawnRoll = INT32_MIN; // the tilt label also shows the PC's dump angle
    v.sorting = pc.sorting;
    v.live = pc.sorting.valid;
    updateSteps();
    updateInfo(pc);
    updateThumb();
    const PcSorting &s = v.sorting;
    const int b = v.live ? binFor(s.material) : -1;
    const bool showTarget = b >= 0 && strcmp(s.step, "idle") && s.dumpRoll != 0;
    if (showTarget) {
        // Dot on the disk edge toward the drop side and rail row.
        const int dx = s.dumpRoll > 0 ? kR : -kR, dy = s.targetRail > 0 ? -kR / 3 : s.targetRail < 0 ? kR / 3 : 0;
        lv_obj_set_pos(v.target, kCx + dx - 9, kCy + dy - 9);
        lv_obj_set_x(v.railTarget, railToX(s.targetRail) - 2);
        lv_obj_remove_flag(v.target, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(v.railTarget, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(v.target, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(v.railTarget, LV_OBJ_FLAG_HIDDEN);
    }
}
