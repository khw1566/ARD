#include "ui_screensaver.h"
#include "ui_layout.h"
#include "ui_text.h"
#include "visitor_voice.h"
#include "nvs.h"

namespace {
constexpr uint32_t kDefaultSeconds = 30;
constexpr uint32_t kSlideMs = 7000; // Longest shipped prompt + microphone echo tail fits one slide.
constexpr uint32_t kFrameMs = 33;   // About 30 fps while visible. The timer is paused while hidden.
constexpr uint32_t kTapMs = 700;    // Ripple lifetime after a pictogram tap.
constexpr int kStageY = 76;         // Pictogram area: cover y 76..392, between heading and help text.
constexpr uint32_t kBg = 0x101E2C, kInk = 0xF2F6FA, kTeal = 0x46D6BE, kDim = 0x455565, kRed = 0xE5484D,
                   kRedPressed = 0x8E1B16, kAmber = 0xF5B83D, kGreen = 0x1E9E50, kGrey = 0x5F6B76;
struct Slide { const char *title; const char *body; };
const Slide slides[] = {
    {"Explore the robot", "Touch the screen to begin. Choose a tab to explore the controls."},
    {"Move with the joystick", "In Drive, press RE-ARM, then hold the joystick to move. Release to stop."},
    {"See how sorting works", "In Main, check the sorting sequence and camera before starting."},
    {"Stay clear of moving parts", "Keep hands away from the robot. Press E-STOP if you need to stop it."},
};
constexpr unsigned kSlideCount = sizeof(slides) / sizeof(slides[0]);

// Pictogram line art. lv_line keeps the pointer, so the arrays stay static.
const lv_point_precise_t kRightChevron[] = {{0, 0}, {12, 14}, {0, 28}};
const lv_point_precise_t kLeftChevron[] = {{12, 0}, {0, 14}, {12, 28}};
const lv_point_precise_t kCheck[] = {{0, 12}, {9, 21}, {24, 3}};
const lv_point_precise_t kSlash[] = {{0, 0}, {84, 84}};
const lv_point_precise_t kTriangle[] = {{4, 62}, {38, 4}, {72, 62}, {4, 62}};
const lv_point_precise_t kUpArrow[] = {{18, 44}, {18, 4}, {4, 18}, {18, 4}, {32, 18}};

// Fingertip pose in stage coordinates. tapAge < 0 means no ripple.
struct Pose { bool visible; int x, y; bool pressed; int tapAge; };

struct TouchScene { lv_obj_t *root, *content, *tab; };
struct DriveScene { lv_obj_t *root, *rearm, *knob, *carriage, *right, *left; };
struct SortScene { lv_obj_t *root, *steps[3], *beam, *lens, *item, *badge, *bin[3]; };
struct SafetyScene { lv_obj_t *root, *carriage, *noSign, *estop; };
TouchScene touchScene;
DriveScene driveScene;
SortScene sortScene;
SafetyScene safetyScene;
lv_obj_t *scenes[kSlideCount];

lv_obj_t *cover, *heading, *help, *dots[kSlideCount], *progress, *hand, *ripple, *estopArrow;
lv_timer_t *frameTimer;
void (*enterCallback)();
void (*wakeCallback)();
uint32_t timeoutSeconds = kDefaultSeconds, started;
unsigned shownSlide = kSlideCount;
bool inhibited;

bool validTimeout(uint32_t seconds) {
    return seconds == 0 || seconds == 15 || seconds == 30 || seconds == 60 || seconds == 300;
}

// ---- shape helpers ------------------------------------------------------------------------------

// All guidance is one touch surface, so no shape, label or line takes the touch.
lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    ui_layout::page(obj, 0);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t *group(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *obj = shape(parent, x, y, w, h, 0, kBg);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    return obj;
}

lv_obj_t *ring(lv_obj_t *parent, int x, int y, int w, int h, int border, int radius, uint32_t color) {
    lv_obj_t *obj = group(parent, x, y, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, border, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(color), 0);
    return obj;
}

lv_obj_t *stroke(lv_obj_t *parent, const lv_point_precise_t *points, uint32_t count, int x, int y, int width,
                 uint32_t color) {
    lv_obj_t *obj = lv_line_create(parent);
    lv_line_set_points(obj, points, count);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_line_width(obj, width, 0);
    lv_obj_set_style_line_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(obj, true, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t *caption(lv_obj_t *parent, const char *value, int x, int y, int w, const lv_font_t *font, uint32_t color,
                  lv_text_align_t align = LV_TEXT_ALIGN_CENTER) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, value);
    lv_obj_set_width(obj, w);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_obj_set_pos(obj, x, y);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

// Per-frame setters touch an object only on a real change, so idle shapes cost no redraw.
void place(lv_obj_t *obj, int x, int y) {
    if (lv_obj_get_x_aligned(obj) != x || lv_obj_get_y_aligned(obj) != y) lv_obj_set_pos(obj, x, y);
}

void show(lv_obj_t *obj, bool visible) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == !visible) return;
    if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void fill(lv_obj_t *obj, uint32_t color) {
    const lv_color_t c = lv_color_hex(color);
    if (!lv_color_eq(lv_obj_get_style_bg_color(obj, 0), c)) lv_obj_set_style_bg_color(obj, c, 0);
}

// Smoothstep from `from` to `to` between t0 and t1 (ms).
int ease(uint32_t t, uint32_t t0, uint32_t t1, int from, int to) {
    if (t <= t0) return from;
    if (t >= t1) return to;
    const int p = static_cast<int>((t - t0) * 1024 / (t1 - t0));
    const int s = p * p / 1024 * (3072 - 2 * p) / 1024;
    return from + (to - from) * s / 1024;
}

int linear(uint32_t t, uint32_t t0, uint32_t t1, int from, int to) {
    if (t <= t0) return from;
    if (t >= t1) return to;
    return from + (to - from) * static_cast<int>(t - t0) / static_cast<int>(t1 - t0);
}

bool within(uint32_t t, uint32_t t0, uint32_t t1) { return t >= t0 && t < t1; }

int tapAge(uint32_t t, uint32_t tap) { return within(t, tap, tap + kTapMs) ? static_cast<int>(t - tap) : -1; }

// Waypoint path for the fingertip: hold each point, then glide to the next.
struct Waypoint { uint32_t at; int x, y; };
template <size_t N> void follow(const Waypoint (&path)[N], uint32_t t, Pose &pose) {
    pose.x = path[0].x;
    pose.y = path[0].y;
    for (size_t i = 1; i < N; ++i) {
        if (t < path[i - 1].at) break;
        pose.x = ease(t, path[i - 1].at, path[i].at, path[i - 1].x, path[i].x);
        pose.y = ease(t, path[i - 1].at, path[i].at, path[i - 1].y, path[i].y);
    }
}

// Pointing hand, palm facing the viewer: the index finger stands on the thumb side and the other
// three fingers are curled as separate knuckles, so it reads as "point and tap".
// Each part has an outline so the fingers stay distinct. The fingertip is at (kTipX, 0).
constexpr int kTipX = 27;
constexpr uint32_t kHandLine = 0x2A3B4C;
lv_obj_t *handPart(lv_obj_t *parent, int x, int y, int w, int h, int radius) {
    lv_obj_t *obj = shape(parent, x, y, w, h, radius, kInk);
    lv_obj_set_style_border_width(obj, 3, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(kHandLine), 0);
    return obj;
}

lv_obj_t *buildHand(lv_obj_t *parent) {
    lv_obj_t *g = group(parent, 0, 0, 108, 150);
    handPart(g, 12, 64, 92, 62, 20);                      // palm
    for (int i = 0; i < 3; ++i) handPart(g, 38 + i * 22, 50, 24, 36, 12); // curled fingers
    handPart(g, 14, 0, 26, 88, 13);                       // index finger
    shape(g, 20, 6, 14, 14, 5, 0xD5E1EB);                 // fingernail
    shape(g, 18, 46, 18, 3, 1, kHandLine);                // knuckle crease
    handPart(g, 0, 80, 46, 24, 12);                       // thumb folded across the palm
    shape(g, 20, 124, 76, 24, 6, kTeal);                  // sleeve
    return g;
}

// Front view of the XY gantry: two posts, an overhead rail and a carriage with a tool.
lv_obj_t *buildGantry(lv_obj_t *parent, int x, lv_obj_t **right = nullptr, lv_obj_t **left = nullptr) {
    shape(parent, x - 10, 262, 384, 8, 4, kDim);
    shape(parent, x, 56, 14, 206, 4, kInk);
    shape(parent, x + 350, 56, 14, 206, 4, kInk);
    shape(parent, x, 56, 364, 16, 6, kInk);
    lv_obj_t *carriage = group(parent, x + 117, 72, 130, 110);
    shape(carriage, 30, 0, 70, 46, 10, kTeal);
    shape(carriage, 59, 46, 12, 50, 4, kInk);
    shape(carriage, 45, 92, 40, 12, 6, kInk);
    if (right) *right = stroke(carriage, kRightChevron, 3, 110, 9, 6, kTeal);
    if (left) *left = stroke(carriage, kLeftChevron, 3, 8, 9, 6, kTeal);
    return carriage;
}

lv_obj_t *buildStage() {
    lv_obj_t *root = group(cover, 0, kStageY, LCD_H_RES, 316);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    return root;
}

// ---- scene 1: touch the screen, then a tab -------------------------------------------------------

void buildTouch() {
    TouchScene &s = touchScene;
    s.root = buildStage();
    ring(s.root, 362, 16, 300, 200, 8, 18, kInk);
    shape(s.root, 482, 216, 60, 20, 0, kInk);
    shape(s.root, 432, 234, 160, 12, 6, kInk);
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *tab = shape(s.root, 386 + i * 88, 40, 76, 28, 6, kDim);
        if (i == 1) s.tab = tab;
    }
    s.content = shape(s.root, 386, 82, 252, 110, 8, kDim);
}

Pose touchFrame(uint32_t t) {
    static const Waypoint path[] = {{0, 760, 176}, {1100, 512, 136}, {2400, 512, 136},
                                    {3300, 512, 54}, {5200, 512, 54}, {6300, 760, 176}};
    Pose pose{true, 0, 0, within(t, 1200, 1450) || within(t, 3400, 3650), -1};
    follow(path, t, pose);
    pose.tapAge = t < 3400 ? tapAge(t, 1200) : tapAge(t, 3400);
    fill(touchScene.content, t >= 1300 ? kTeal : kDim);
    fill(touchScene.tab, t >= 3500 ? kTeal : kDim);
    return pose;
}

// ---- scene 2: RE-ARM, hold the joystick, release to stop -----------------------------------------

void buildDrive() {
    DriveScene &s = driveScene;
    s.root = buildStage();
    s.rearm = shape(s.root, 70, 128, 150, 60, 12, kGrey);
    caption(s.rearm, "RE-ARM", 0, 17, 150, &ard_font_20, kInk);
    ring(s.root, 290, 84, 150, 150, 8, LV_RADIUS_CIRCLE, kInk);
    shape(s.root, 361, 108, 8, 102, 4, kDim);
    shape(s.root, 314, 155, 102, 8, 4, kDim);
    s.knob = shape(s.root, 333, 127, 64, 64, LV_RADIUS_CIRCLE, kTeal);
    s.carriage = buildGantry(s.root, 580, &s.right, &s.left);
}

Pose driveFrame(uint32_t t) {
    DriveScene &s = driveScene;
    fill(s.rearm, t >= 800 ? kGreen : kGrey);
    // Knob offset: right, then left, then a spring back to the centre on release.
    int knob = ease(t, 2000, 2400, 0, 40);
    if (t >= 4200) knob = ease(t, 4200, 4700, 40, -40);
    if (t >= 5600) knob = ease(t, 5600, 5900, -40, 0);
    place(s.knob, 333 + knob, 127);
    // The carriage moves only while the joystick is held, at a steady speed.
    int carriage = linear(t, 2200, 4300, 0, 110);
    if (t >= 4300) carriage = linear(t, 4300, 5600, 110, -50);
    place(s.carriage, 580 + 117 + carriage, 72);
    show(s.right, within(t, 2200, 4300) && (t / 250) % 2 == 0);
    show(s.left, within(t, 4500, 5600) && (t / 250) % 2 == 0);

    static const Waypoint path[] = {{0, 145, 250}, {600, 145, 158}, {1200, 145, 158},
                                    {2000, 365, 159}, {5600, 365, 159}, {6500, 365, 250}};
    Pose pose{true, 0, 0, within(t, 700, 950) || within(t, 2000, 5600), tapAge(t, 700)};
    follow(path, t, pose);
    if (within(t, 2000, 5600)) pose.x = 365 + knob; // The finger rides on the knob while it holds.
    if (t >= 5600 && t < 6500) pose.x = ease(t, 5600, 5900, 365 - 40, 365);
    return pose;
}

// ---- scene 3: feed, photograph, sort into a bin --------------------------------------------------

void buildSort() {
    SortScene &s = sortScene;
    s.root = buildStage();
    shape(s.root, 150, 18, 140, 4, 2, kDim);
    for (int i = 0; i < 3; ++i) {
        s.steps[i] = shape(s.root, 130 + i * 70, 0, 40, 40, LV_RADIUS_CIRCLE, kDim);
        static const char *const numbers[] = {"1", "2", "3"};
        caption(s.steps[i], numbers[i], 0, 9, 40, &ard_font_20, kInk);
    }
    s.beam = shape(s.root, 487, 130, 50, 76, 6, kTeal);
    lv_obj_set_style_bg_opa(s.beam, LV_OPA_30, 0);
    shape(s.root, 492, 52, 40, 16, 4, kInk);
    shape(s.root, 462, 64, 100, 66, 12, kInk);
    s.lens = shape(s.root, 489, 74, 46, 46, LV_RADIUS_CIRCLE, kDim);
    lv_obj_set_style_border_width(s.lens, 7, 0);
    lv_obj_set_style_border_color(s.lens, lv_color_hex(kBg), 0);
    shape(s.root, 130, 250, 590, 12, 6, kInk);
    for (int i = 0; i < 5; ++i) shape(s.root, 150 + i * 130, 266, 18, 18, LV_RADIUS_CIRCLE, kDim);
    static const int walls[][4] = {{0, 0, 10, 90}, {100, 0, 10, 90}, {0, 80, 110, 10}};
    for (int i = 0; i < 3; ++i) {
        s.bin[i] = shape(s.root, 760 + walls[i][0], 196 + walls[i][1], walls[i][2], walls[i][3], 4, kDim);
        shape(s.root, 890 + walls[i][0], 196 + walls[i][1], walls[i][2], walls[i][3], 4, kDim);
    }
    s.item = shape(s.root, 150, 206, 44, 44, LV_RADIUS_CIRCLE, kAmber);
    s.badge = shape(s.root, 0, 0, 40, 40, LV_RADIUS_CIRCLE, kGreen);
    stroke(s.badge, kCheck, 3, 8, 9, 5, kInk);
}

Pose sortFrame(uint32_t t) {
    SortScene &s = sortScene;
    const unsigned step = t < 1500 ? 0 : t < 3200 ? 1 : 2;
    for (unsigned i = 0; i < 3; ++i) fill(s.steps[i], i == step ? kTeal : kDim);
    show(s.beam, within(t, 1600, 2800) && (t / 200) % 3 != 0);
    fill(s.lens, within(t, 1700, 1850) || within(t, 2300, 2450) ? kTeal : kDim);
    int x = ease(t, 0, 1500, 150, 490), y = 206;
    if (t >= 3300) x = ease(t, 3300, 4000, 490, 720);
    if (t >= 4000) {
        x = ease(t, 4000, 4500, 720, 793);
        y = t < 4300 ? ease(t, 4000, 4300, 206, 150) : ease(t, 4300, 4700, 150, 232);
    }
    place(s.item, x, y);
    show(s.badge, t >= 2800);
    place(s.badge, x + 30, y - 28);
    for (lv_obj_t *wall : s.bin) fill(wall, t >= 4700 ? kTeal : kDim);
    return Pose{false, 0, 0, false, -1};
}

// ---- scene 4: hands off the moving robot, E-STOP stops it ----------------------------------------

void buildSafety() {
    SafetyScene &s = safetyScene;
    s.root = buildStage();
    s.carriage = buildGantry(s.root, 110);
    stroke(s.root, kTriangle, 4, 516, 0, 7, kAmber);
    caption(s.root, "!", 516, 22, 76, &ard_font_28, kAmber);
    ring(s.root, 770, 70, 150, 150, 14, LV_RADIUS_CIRCLE, kAmber);
    s.estop = shape(s.root, 789, 89, 112, 112, LV_RADIUS_CIRCLE, kRed);
    caption(s.estop, "E-STOP", 0, 44, 112, &ard_font_20, kInk);
    s.noSign = ring(s.root, 510, 125, 130, 130, 12, LV_RADIUS_CIRCLE, kRed);
    stroke(s.noSign, kSlash, 2, 11, 11, 12, kRed);
}

int sway(uint32_t t) {
    const uint32_t phase = t % 2400;
    return phase < 1200 ? ease(phase, 0, 1200, -100, 100) : ease(phase, 1200, 2400, 100, -100);
}

Pose safetyFrame(uint32_t t) {
    SafetyScene &s = safetyScene;
    constexpr uint32_t kStopAt = 4300;
    place(s.carriage, 110 + 117 + sway(t < kStopAt ? t : kStopAt), 72);
    show(s.noSign, within(t, 1200, 2800) && (t / 300) % 4 != 3);
    fill(s.estop, t >= kStopAt ? kRedPressed : kRed);
    static const Waypoint path[] = {{0, 800, 180}, {1300, 562, 150}, {2000, 562, 150}, {2800, 690, 180},
                                    {3000, 690, 180}, {4000, 845, 140}, {5600, 845, 140}, {6600, 960, 190}};
    Pose pose{true, 0, 0, within(t, 4200, 4500), tapAge(t, 4200)};
    follow(path, t, pose);
    return pose;
}

Pose (*const frames[kSlideCount])(uint32_t) = {touchFrame, driveFrame, sortFrame, safetyFrame};

// ---- cover --------------------------------------------------------------------------------------

void hide(bool wake) {
    if (!ui_screensaver_active()) return;
    lv_obj_add_flag(cover, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(frameTimer);
    visitor_voice_cancel();
    lv_display_trigger_activity(nullptr);
    if (wake && wakeCallback) wakeCallback();
}

void touched(lv_event_t *event) {
    // Wait until release: the wake gesture cannot press an underlying control.
    lv_indev_t *indev = lv_event_get_indev(event);
    if (indev) lv_indev_wait_release(indev);
    hide(true);
}

void animate() {
    const uint32_t elapsed = lv_tick_elaps(started);
    const unsigned slide = (elapsed / kSlideMs) % kSlideCount;
    const uint32_t t = elapsed % kSlideMs;
    if (slide != shownSlide) {
        shownSlide = slide;
        lv_label_set_text(heading, slides[slide].title);
        lv_label_set_text(help, slides[slide].body);
        const VisitorVoice voices[] = {VisitorVoice::Welcome, VisitorVoice::Drive, VisitorVoice::Camera, VisitorVoice::Safety};
        visitor_voice_request(voices[slide], ui_language() == UI_LANG_KO);
        for (unsigned i = 0; i < kSlideCount; ++i) {
            lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == slide ? kTeal : kDim), 0);
            show(scenes[i], i == slide);
        }
    }
    const Pose pose = frames[slide](t);
    show(hand, pose.visible);
    const int tipY = kStageY + pose.y + (pose.pressed ? 8 : 0);
    if (pose.visible) place(hand, pose.x - kTipX, tipY);
    show(ripple, pose.visible && pose.tapAge >= 0);
    if (pose.visible && pose.tapAge >= 0) {
        const int size = 24 + pose.tapAge * 130 / static_cast<int>(kTapMs);
        lv_obj_set_size(ripple, size, size);
        place(ripple, pose.x - size / 2, tipY - size / 2);
        lv_obj_set_style_border_opa(ripple, static_cast<lv_opa_t>(255 - pose.tapAge * 255 / static_cast<int>(kTapMs)), 0);
    }
    // The arrow keeps pointing at the real E-STOP button in the top bar.
    const uint32_t bob = elapsed % 1200;
    place(estopArrow, 904, 4 + static_cast<int>(bob < 600 ? bob : 1200 - bob) * 8 / 600);
    lv_obj_set_width(progress, 1 + t * 719 / kSlideMs);
}

void frame(lv_timer_t *) {
    if (ui_screensaver_active()) animate();
}
} // namespace

void ui_screensaver_init(void (*onEnter)(), void (*onWake)()) {
    enterCallback = onEnter;
    wakeCallback = onWake;
    nvs_handle_t nvs;
    uint32_t saved = kDefaultSeconds;
    if (nvs_open("ard-ui", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u32(nvs, "saver_sec", &saved);
        nvs_close(nvs);
    }
    timeoutSeconds = validTimeout(saved) ? saved : kDefaultSeconds;
    cover = lv_obj_create(lv_layer_top());
    ui_layout::page(cover, 0);
    lv_obj_set_pos(cover, 0, ui_layout::kBarHeight);
    lv_obj_set_size(cover, LCD_H_RES, LCD_V_RES - ui_layout::kBarHeight);
    lv_obj_set_style_radius(cover, 0, 0);
    lv_obj_set_style_bg_color(cover, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
    lv_obj_add_flag(cover, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cover, touched, LV_EVENT_RELEASED, nullptr);
    heading = caption(cover, "", 232, 22, LCD_H_RES - 464, &ard_font_28, kInk);
    caption(cover, "E-STOP", 760, 16, 136, &ard_font_20, kRed, LV_TEXT_ALIGN_RIGHT);
    estopArrow = stroke(cover, kUpArrow, 5, 904, 4, 6, kRed);
    buildTouch();
    buildDrive();
    buildSort();
    buildSafety();
    scenes[0] = touchScene.root;
    scenes[1] = driveScene.root;
    scenes[2] = sortScene.root;
    scenes[3] = safetyScene.root;
    ripple = ring(cover, 0, 0, 24, 24, 5, LV_RADIUS_CIRCLE, kTeal);
    hand = buildHand(cover);
    help = caption(cover, "", 48, 398, LCD_H_RES - 96, &ard_font_20, 0xD5E1EB);
    for (unsigned i = 0; i < kSlideCount; ++i) {
        dots[i] = shape(cover, (LCD_H_RES - 110) / 2 + i * 32, 460, 14, 14, LV_RADIUS_CIRCLE, kDim);
    }
    progress = shape(cover, (LCD_H_RES - 720) / 2, 482, 1, 4, 2, kTeal);
    caption(cover, "Help animation only. The robot does not move with this animation.", 48, 496, LCD_H_RES - 96,
            &ard_font_16, 0xA7BCCB);
    frameTimer = lv_timer_create(frame, kFrameMs, nullptr);
    lv_timer_pause(frameTimer);
    lv_obj_add_flag(cover, LV_OBJ_FLAG_HIDDEN);
    lv_display_trigger_activity(nullptr);
}

bool ui_screensaver_active() { return cover && !lv_obj_has_flag(cover, LV_OBJ_FLAG_HIDDEN); }
uint32_t ui_screensaver_timeout() { return timeoutSeconds; }

esp_err_t ui_screensaver_set_timeout(uint32_t seconds) {
    if (!validTimeout(seconds)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ard-ui", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(nvs, "saver_sec", seconds);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) {
        timeoutSeconds = seconds;
        lv_display_trigger_activity(nullptr);
    }
    return err;
}

void ui_screensaver_enter(lv_event_t *) {
    if (!cover || inhibited || ui_screensaver_active()) return;
    if (enterCallback) enterCallback();
    started = lv_tick_get();
    shownSlide = kSlideCount;
    lv_obj_move_foreground(cover); // Cover any open settings keyboard as well.
    lv_obj_remove_flag(cover, LV_OBJ_FLAG_HIDDEN);
    animate();
    lv_timer_resume(frameTimer);
}

void ui_screensaver_poll(bool blocked) {
    inhibited = blocked;
    if (blocked) {
        hide(false); // Approval and board notices take precedence over the help screen.
        lv_display_trigger_activity(nullptr);
        return;
    }
    if (ui_screensaver_active()) return; // The frame timer animates while visible.
    // A stationary held joystick or TALK button still counts as interaction.
    for (lv_indev_t *input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) {
        if (lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED) return;
    }
    if (timeoutSeconds && lv_display_get_inactive_time(nullptr) >= timeoutSeconds * 1000)
        ui_screensaver_enter();
}
