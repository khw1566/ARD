#include "ui_settings.h"
#include "ui_text.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "ard_network.h"
#include "ard_wifi.h"
#include "bsp_display.h"
#include "esp_wifi.h"
#include "onboard_audio.h"
#include "pc_link.h"
#include "ui.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace {

enum class Action { Scan, Join };
enum class Phase { Idle, Scanning, Joining, ScanDone, Joined, Failed };
struct Request { Action action; char ssid[33]; char password[65]; };
struct Result {
    Phase phase;
    Action action;
    esp_err_t error;
    uint32_t generation;
    uint16_t count;
    ard_network_ap_t aps[ARD_WIFI_SCAN_MAX];
    uint32_t brightnessGeneration;
    esp_err_t brightnessError;
    bool brightnessBusy;
    uint32_t serverGeneration;
    esp_err_t serverError;
    bool serverBusy;
};

QueueHandle_t requests;
portMUX_TYPE resultLock = portMUX_INITIALIZER_UNLOCKED;
Result result{};
int pendingBrightness = -1;
char pendingServer[65];
bool serverPending;
bool ready;
struct Widgets {
    lv_obj_t *tabs, *brightness, *brightnessValue, *brightnessStatus;
    lv_obj_t *scan, *list, *ssid, *password, *connect, *status, *connection, *keyboard;
    lv_obj_t *server, *serverSave, *serverStatus, *serverCurrent;
    lv_obj_t *volume, *volumeValue, *mute, *soundStatus;
    ard_network_ap_t aps[ARD_WIFI_SCAN_MAX];
    uint16_t count;
    uint32_t generation = UINT32_MAX, brightnessGeneration = UINT32_MAX, serverGeneration = UINT32_MAX;
    bool savedSsidLoaded, serverLoaded, soundMuted;
} ui;

const lv_color_t accent = lv_color_hex(0x0B6E99);

void clearSecret(char *text, size_t size) {
    volatile char *p = text;
    while (size--) *p++ = 0;
}

void worker(void *) {
    for (;;) {
        int brightness;
        taskENTER_CRITICAL(&resultLock);
        brightness = pendingBrightness;
        pendingBrightness = -1;
        taskEXIT_CRITICAL(&resultLock);
        if (brightness >= 0) {
            const esp_err_t err = bsp_backlight_save(brightness);
            taskENTER_CRITICAL(&resultLock);
            result.brightnessError = err;
            result.brightnessBusy = pendingBrightness >= 0;
            ++result.brightnessGeneration;
            taskEXIT_CRITICAL(&resultLock);
        }

        char server[65];
        taskENTER_CRITICAL(&resultLock);
        const bool saveServer = serverPending;
        serverPending = false;
        if (saveServer) std::memcpy(server, pendingServer, sizeof(server));
        taskEXIT_CRITICAL(&resultLock);
        if (saveServer) {
            const esp_err_t err = ard_network_set_server(server);
            taskENTER_CRITICAL(&resultLock);
            result.serverError = err;
            result.serverBusy = serverPending;
            ++result.serverGeneration;
            taskEXIT_CRITICAL(&resultLock);
        }

        Request request{};
        if (xQueueReceive(requests, &request, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        ard_network_ap_t aps[ARD_WIFI_SCAN_MAX]{};
        uint16_t count = 0;
        const esp_err_t err = request.action == Action::Scan
            ? ard_network_scan(aps, ARD_WIFI_SCAN_MAX, &count)
            : ard_network_join(request.ssid, request.password);
        taskENTER_CRITICAL(&resultLock);
        result.error = err;
        if (request.action == Action::Scan) {
            result.count = count;
            std::memcpy(result.aps, aps, sizeof(aps));
        }
        result.phase = err != ESP_OK ? Phase::Failed
            : request.action == Action::Scan ? Phase::ScanDone : Phase::Joined;
        ++result.generation;
        taskEXIT_CRITICAL(&resultLock);
        clearSecret(request.password, sizeof(request.password));
    }
}

lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y, int width,
               const lv_font_t *font = &ard_font_20) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, value);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x1F2A36), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

lv_obj_t *button(lv_obj_t *parent, const char *caption, int x, int y, int width, lv_event_cb_t callback,
                 void *data = nullptr) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, 46);
    lv_obj_set_style_bg_color(button, accent, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, data);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, caption);
    lv_obj_set_style_text_font(label, &ard_font_20, 0);
    lv_obj_center(label);
    return button;
}

void hideKeyboard() {
    if (!ui.keyboard) return;
    lv_keyboard_set_textarea(ui.keyboard, nullptr);
    lv_obj_add_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN);
}

void requestServer(const char *host);

void keyboardEvent(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
    const bool server = lv_keyboard_get_textarea(ui.keyboard) == ui.server;
    hideKeyboard();
    // OK on the server keyboard saves, like the Save button.
    if (code == LV_EVENT_READY && server) requestServer(lv_textarea_get_text(ui.server));
}

void showKeyboard(lv_obj_t *field, lv_keyboard_mode_t mode) {
    lv_keyboard_set_mode(ui.keyboard, mode);
    lv_keyboard_set_textarea(ui.keyboard, field);
    lv_obj_clear_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.keyboard);
}

void editField(lv_event_t *event) {
    lv_obj_t *field = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (field == ui.server) {
        // Keep the current mode if the ABC/123 key already chose one.
        if (lv_keyboard_get_textarea(ui.keyboard) != field) showKeyboard(field, LV_KEYBOARD_MODE_NUMBER);
        return;
    }
    taskENTER_CRITICAL(&resultLock);
    const bool busy = result.phase == Phase::Scanning || result.phase == Phase::Joining;
    taskEXIT_CRITICAL(&resultLock);
    if (busy) return;
    showKeyboard(field, LV_KEYBOARD_MODE_TEXT_LOWER);
}

void settingsTabChanged(lv_event_t *) { hideKeyboard(); }

bool submit(const Request &request, Phase phase) {
    taskENTER_CRITICAL(&resultLock);
    const bool busy = result.phase == Phase::Scanning || result.phase == Phase::Joining;
    if (!busy) { result.phase = phase; result.action = request.action; result.error = ESP_OK; ++result.generation; }
    taskEXIT_CRITICAL(&resultLock);
    if (busy) return false;
    if (xQueueSend(requests, &request, 0) == pdTRUE) return true;
    taskENTER_CRITICAL(&resultLock);
    result.phase = Phase::Failed;
    result.error = ESP_ERR_NO_MEM;
    ++result.generation;
    taskEXIT_CRITICAL(&resultLock);
    return false;
}

void scan(lv_event_t *) {
    hideKeyboard();
    Request request{};
    request.action = Action::Scan;
    submit(request, Phase::Scanning);
}

void selectNetwork(lv_event_t *event) {
    taskENTER_CRITICAL(&resultLock);
    const bool busy = result.phase == Phase::Scanning || result.phase == Phase::Joining;
    taskEXIT_CRITICAL(&resultLock);
    if (busy) return;
    const size_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)) - 1;
    if (index >= ui.count) return;
    if (std::strcmp(lv_textarea_get_text(ui.ssid), ui.aps[index].ssid)) lv_textarea_set_text(ui.password, "");
    lv_textarea_set_text(ui.ssid, ui.aps[index].ssid);
    ard_wifi_profile_t saved{};
    const bool known = ard_wifi_saved_profile(ui.aps[index].ssid, &saved);
    if (known) lv_textarea_set_text(ui.password, saved.password);
    clearSecret(saved.password, sizeof(saved.password));
    // An open network must not inherit a password from a previous selection.
    if (!ui.aps[index].secure) lv_textarea_set_text(ui.password, "");
    lv_label_set_text(ui.status, !ui.aps[index].secure ? "Open network selected. Press Connect."
                      : known ? "Saved password loaded. Check it, then Connect."
                      : "Enter the Wi-Fi password, then Connect.");
    hideKeyboard();
}

void connect(lv_event_t *) {
    hideKeyboard();
    Request request{};
    request.action = Action::Join;
    const char *ssid = lv_textarea_get_text(ui.ssid), *password = lv_textarea_get_text(ui.password);
    // Limits are bytes, not LVGL's Unicode character count.
    const size_t ssidBytes = std::strlen(ssid), passwordBytes = std::strlen(password);
    if (!ssidBytes || ssidBytes > 32) {
        lv_label_set_text(ui.status, "Enter a Wi-Fi name of 1 to 32 bytes.");
        return;
    }
    ard_network_settings_t candidate{};
    ard_network_get_settings(&candidate);
    if (candidate.version != 1) {
        lv_label_set_text(ui.status, "Wi-Fi is still starting. Wait a moment and try again.");
        return;
    }
    strlcpy(candidate.ssid, ssid, sizeof(candidate.ssid));
    if (passwordBytes > 64) {
        lv_label_set_text(ui.status, "Password is too long (maximum 64 bytes).");
        clearSecret(candidate.password, sizeof(candidate.password));
        return;
    }
    strlcpy(candidate.password, password, sizeof(candidate.password));
    if (!ard_network_valid(&candidate)) {
        lv_label_set_text(ui.status, "Password: 8-63 characters, 64 hex digits, or empty for open Wi-Fi.");
        clearSecret(candidate.password, sizeof(candidate.password));
        return;
    }
    strlcpy(request.ssid, ssid, sizeof(request.ssid));
    strlcpy(request.password, password, sizeof(request.password));
    submit(request, Phase::Joining);
    clearSecret(request.password, sizeof(request.password));
    clearSecret(candidate.password, sizeof(candidate.password));
}

void saveBrightness() {
    if (!ready) {
        lv_label_set_text(ui.brightnessStatus, "Brightness changed, but settings storage is unavailable.");
        return;
    }
    taskENTER_CRITICAL(&resultLock);
    pendingBrightness = bsp_backlight_get(); // coalesce rapid slider changes
    result.brightnessBusy = true;
    taskEXIT_CRITICAL(&resultLock);
    lv_label_set_text(ui.brightnessStatus, "Saving brightness...");
}

void brightnessChanged(lv_event_t *event) {
    bsp_backlight_set(lv_slider_get_value(ui.brightness));
    lv_label_set_text_fmt(ui.brightnessValue, "%d %%", bsp_backlight_get());
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) saveBrightness();
}

void brightnessPreset(lv_event_t *event) {
    const int value = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    lv_slider_set_value(ui.brightness, value, LV_ANIM_OFF);
    bsp_backlight_set(value);
    lv_label_set_text_fmt(ui.brightnessValue, "%d %%", value);
    saveBrightness();
}

void requestServer(const char *host) {
    if (!ready) {
        lv_label_set_text(ui.serverStatus, "Settings storage is unavailable. Restart the board.");
        return;
    }
    if (!ard_network_valid_server(host)) {
        lv_label_set_text(ui.serverStatus, "Enter an IP address such as 192.168.0.35 or a host name, "
                          "without http://, port or path.");
        return;
    }
    taskENTER_CRITICAL(&resultLock);
    strlcpy(pendingServer, host, sizeof(pendingServer));
    serverPending = true;
    result.serverBusy = true;
    taskEXIT_CRITICAL(&resultLock);
    lv_obj_add_state(ui.serverSave, LV_STATE_DISABLED);
    lv_label_set_text(ui.serverStatus, "Saving PC server...");
}

void saveServer(lv_event_t *) {
    hideKeyboard();
    requestServer(lv_textarea_get_text(ui.server));
}

void defaultServer(lv_event_t *) {
    hideKeyboard();
    lv_textarea_set_text(ui.server, CONFIG_ARD_DEFAULT_SERVER);
    requestServer(CONFIG_ARD_DEFAULT_SERVER);
}

void serverKeys(lv_event_t *) {
    // Number pad for IP addresses; letters for names such as my-pc.local.
    const bool number = lv_keyboard_get_textarea(ui.keyboard) == ui.server &&
                        !lv_obj_has_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN) &&
                        lv_keyboard_get_mode(ui.keyboard) == LV_KEYBOARD_MODE_NUMBER;
    showKeyboard(ui.server, number ? LV_KEYBOARD_MODE_TEXT_LOWER : LV_KEYBOARD_MODE_NUMBER);
}

void buildServer(lv_obj_t *page) {
    // The keyboard covers the lower part of this tab; keep the field near the top.
    text(page, "PC server address", 24, 12, 600, &ard_font_28);
    ui.server = lv_textarea_create(page);
    lv_obj_set_pos(ui.server, 24, 56);
    lv_obj_set_size(ui.server, 520, 46);
    lv_obj_set_style_text_font(ui.server, &ard_font_20, 0);
    lv_textarea_set_one_line(ui.server, true);
    lv_textarea_set_max_length(ui.server, 63);
    lv_textarea_set_placeholder_text(ui.server, "e.g. 192.168.0.35");
    lv_obj_add_event_cb(ui.server, editField, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(ui.server, editField, LV_EVENT_CLICKED, nullptr);
    ui.serverSave = button(page, "Save", 560, 56, 130, saveServer);
    button(page, "ABC/123", 706, 56, 130, serverKeys);
    button(page, "Default", 852, 56, 140, defaultServer);
    ui.serverStatus = text(page, "Enter the IP address of the PC running ARD, then Save.", 24, 118, 968);
    ui.serverCurrent = text(page, "", 24, 176, 968, &ard_font_16);
    text(page, "Applied immediately and kept after restart. Default: " CONFIG_ARD_DEFAULT_SERVER
         ". Serial console: server=<IP>", 24, 214, 968, &ard_font_16);
}

constexpr int kVolumeMax = 21; // ES8311 output steps, same scale as the PC voice settings

void showVolume(int value) {
    lv_label_set_text_fmt(ui.volumeValue, "%d / %d", value, kVolumeMax);
}

// The PC pushes its stored volume to the selected voice board; keep both equal.
// Another voice board (ControlMCU) has its own PC volume, so only change it locally then.
void saveVolume() {
    const int value = lv_slider_get_value(ui.volume);
    const OnboardAudioStatus audio = onboard_audio_status();
    if (!audio.selected) {
        lv_label_set_text(ui.soundStatus, "Saved on this board. The PC uses another voice board.");
        return;
    }
    char command[12];
    std::snprintf(command, sizeof(command), "vol=%d", value);
    lv_label_set_text(ui.soundStatus, pc_send_command(PC_SOUND, command)
                      ? "Saved on this board and sent to the PC." : "Saved on this board. PC busy; try again.");
}

void setVolume(int value) {
    value = value < 0 ? 0 : value > kVolumeMax ? kVolumeMax : value;
    onboard_audio_set_volume(value);
    lv_slider_set_value(ui.volume, value, LV_ANIM_OFF);
    showVolume(value);
}

void volumeChanged(lv_event_t *event) {
    setVolume(lv_slider_get_value(ui.volume));
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) saveVolume();
}

void volumeStep(lv_event_t *event) {
    setVolume(lv_slider_get_value(ui.volume) + static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
    saveVolume();
}

void muteToggle(lv_event_t *) {
    onboard_audio_command(onboard_audio_status().muted ? "unmute" : "mute");
}

void soundTest(lv_event_t *) {
    onboard_audio_test();
    lv_label_set_text(ui.soundStatus, "Playing test sound...");
}

void buildSound(lv_obj_t *page) {
    text(page, "Speaker volume", 24, 20, 600, &ard_font_28);
    text(page, "Applies immediately. Saved on the board and in the PC voice settings.", 24, 66, 900);
    const int current = onboard_audio_status().volume;
    ui.volumeValue = text(page, "", 825, 116, 150, &ard_font_28);
    showVolume(current);
    ui.volume = lv_slider_create(page);
    lv_obj_set_pos(ui.volume, 42, 130);
    lv_obj_set_size(ui.volume, 720, 22);
    lv_slider_set_range(ui.volume, 0, kVolumeMax);
    lv_slider_set_value(ui.volume, current, LV_ANIM_OFF);
    lv_obj_add_event_cb(ui.volume, volumeChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(ui.volume, volumeChanged, LV_EVENT_RELEASED, nullptr);
    button(page, "-", 24, 194, 130, volumeStep, reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
    button(page, "+", 174, 194, 130, volumeStep, reinterpret_cast<void *>(static_cast<intptr_t>(1)));
    ui.mute = button(page, "Mute", 324, 194, 170, muteToggle);
    button(page, "Test sound", 514, 194, 190, soundTest);
    ui.soundStatus = text(page, "", 24, 274, 900);
    text(page, "Same setting as Voice tab VOL - / + and the PC voice volume. 0 is silent.", 24, 326, 900,
         &ard_font_16);
}

void restartForLanguage(lv_timer_t *) { esp_restart(); }

void chooseLanguage(lv_event_t *event) {
    const UiLanguage next = static_cast<UiLanguage>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    if (next == ui_language()) return;
    if (ui_save_language(next) != ESP_OK) {
        lv_label_set_text(ui.brightnessStatus, "Could not save the language. Try again.");
        return;
    }
    // Every screen is built once in one language, so restart to rebuild it.
    ui_notice("Restarting to change the language..."); // shown in the current language
    lv_timer_set_repeat_count(lv_timer_create(restartForLanguage, 800, nullptr), 1);
}

void buildDisplay(lv_obj_t *page) {
    text(page, "Screen brightness", 24, 20, 600, &ard_font_28);
    text(page, "Adjust with the slider. Your setting is saved when released.", 24, 66, 900);
    ui.brightnessValue = text(page, "", 825, 116, 130, &ard_font_28);
    lv_label_set_text_fmt(ui.brightnessValue, "%d %%", bsp_backlight_get());
    ui.brightness = lv_slider_create(page);
    lv_obj_set_pos(ui.brightness, 42, 130);
    lv_obj_set_size(ui.brightness, 720, 22);
    lv_slider_set_range(ui.brightness, 5, 100);
    lv_slider_set_value(ui.brightness, bsp_backlight_get(), LV_ANIM_OFF);
    lv_obj_add_event_cb(ui.brightness, brightnessChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(ui.brightness, brightnessChanged, LV_EVENT_RELEASED, nullptr);
    const int presets[] = {10, 25, 50, 80, 100};
    for (int i = 0; i < 5; ++i) {
        char caption[12];
        std::snprintf(caption, sizeof(caption), "%d %%", presets[i]);
        button(page, caption, 24 + i * 150, 194, 130, brightnessPreset, reinterpret_cast<void *>(static_cast<intptr_t>(presets[i])));
    }
    ui.brightnessStatus = text(page, "Brightness is restored after restarting the board.", 24, 274, 900);
    // Language names stay in their own language so either can be found.
    text(page, "Language", 24, 372, 160);
    lv_obj_t *english = button(page, "English", 190, 360, 150, chooseLanguage,
                               reinterpret_cast<void *>(static_cast<intptr_t>(UI_LANG_EN)));
    lv_obj_t *korean = button(page, "한국어", 356, 360, 150, chooseLanguage,
                              reinterpret_cast<void *>(static_cast<intptr_t>(UI_LANG_KO)));
    lv_obj_add_state(ui_language() == UI_LANG_KO ? korean : english, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(ui_language() == UI_LANG_KO ? korean : english, lv_color_hex(0x1E9E50), LV_STATE_CHECKED);
    text(page, "The board restarts to apply the language.", 524, 372, 470, &ard_font_16);
    text(page, "Minimum brightness is 5% so the controls remain visible.", 24, 326, 900, &ard_font_16);
}

lv_obj_t *input(lv_obj_t *parent, int y, uint32_t maximum, const char *placeholder) {
    lv_obj_t *field = lv_textarea_create(parent);
    lv_obj_set_pos(field, 368, y);
    lv_obj_set_size(field, 600, 46);
    lv_obj_set_style_text_font(field, &ard_font_20, 0);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_max_length(field, maximum);
    lv_textarea_set_placeholder_text(field, placeholder);
    lv_obj_add_event_cb(field, editField, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(field, editField, LV_EVENT_CLICKED, nullptr);
    return field;
}

void buildNetwork(lv_obj_t *page) {
    text(page, "Nearby Wi-Fi", 16, 10, 300, &ard_font_28);
    ui.scan = button(page, "Search Wi-Fi", 16, 54, 318, scan);
    ui.list = lv_list_create(page);
    lv_obj_set_pos(ui.list, 16, 112);
    lv_obj_set_size(ui.list, 318, 284);
    text(ui.list, "Press Search Wi-Fi", 0, 0, 270, &ard_font_16);
    ui.connection = text(page, "Wi-Fi starting...", 368, 8, 600, &ard_font_16);
    text(page, "Wi-Fi name (SSID)", 368, 52, 590);
    ui.ssid = input(page, 80, 32, "Choose a network or enter its name");
    text(page, "Password", 368, 128, 590);
    ui.password = input(page, 154, 64, "Wi-Fi password");
    lv_textarea_set_password_mode(ui.password, false);
    text(page, "Password is shown as entered.", 368, 226, 400, &ard_font_16);
    ui.connect = button(page, "Connect", 778, 214, 190, connect);
    ui.status = text(page, "Select a 2.4 GHz network. Connection details are saved after success.", 368, 276, 600);
    text(page, "The setup access point turns off while connecting.", 368, 358, 600, &ard_font_16);
}

} // namespace

esp_err_t ui_settings_create(lv_obj_t *page) {
    requests = xQueueCreate(1, sizeof(Request));
    if (!requests) return ESP_ERR_NO_MEM;
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    ui.tabs = lv_tabview_create(page);
    lv_obj_set_size(ui.tabs, LV_PCT(100), LV_PCT(100));
    lv_tabview_set_tab_bar_size(ui.tabs, 48);
    lv_obj_set_style_text_font(lv_tabview_get_tab_bar(ui.tabs), &ard_font_20, 0);
    lv_obj_t *display = lv_tabview_add_tab(ui.tabs, "Display");
    lv_obj_t *sound = lv_tabview_add_tab(ui.tabs, "Sound");
    lv_obj_t *network = lv_tabview_add_tab(ui.tabs, "Network");
    lv_obj_t *server = lv_tabview_add_tab(ui.tabs, "Server");
    for (lv_obj_t *tab : {display, sound, network, server}) {
        lv_obj_set_style_pad_all(tab, 0, 0);
        lv_obj_set_style_bg_color(tab, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
        lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    }
    // Nested tabs use their buttons; keep horizontal gestures for the main tabs.
    lv_obj_clear_flag(lv_tabview_get_content(ui.tabs), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ui.tabs, settingsTabChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    buildDisplay(display);
    buildSound(sound);
    buildNetwork(network);
    buildServer(server);
    ui.keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(ui.keyboard, LV_PCT(100), 220);
    lv_obj_align(ui.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(ui.keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(ui.keyboard, keyboardEvent, LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    if (xTaskCreatePinnedToCore(worker, "ui_settings", 8192, nullptr, 2, nullptr, 0) != pdPASS) {
        lv_obj_add_state(ui.scan, LV_STATE_DISABLED);
        lv_obj_add_state(ui.connect, LV_STATE_DISABLED);
        lv_obj_add_state(ui.brightness, LV_STATE_DISABLED);
        lv_obj_add_state(ui.serverSave, LV_STATE_DISABLED);
        lv_label_set_text(ui.status, "Settings worker could not start. Restart the board.");
        return ESP_ERR_NO_MEM;
    }
    ready = true;
    return ESP_OK;
}

void ui_settings_poll(bool visible) {
    if (!ready) return;
    if (!visible) {
        hideKeyboard();
        lv_textarea_set_password_mode(ui.password, false);
    }
    Result snapshot;
    taskENTER_CRITICAL(&resultLock);
    snapshot = result;
    taskEXIT_CRITICAL(&resultLock);
    if (!snapshot.brightnessBusy && snapshot.brightnessGeneration != ui.brightnessGeneration) {
        ui.brightnessGeneration = snapshot.brightnessGeneration;
        if (snapshot.brightnessGeneration)
            lv_label_set_text(ui.brightnessStatus, snapshot.brightnessError == ESP_OK
                              ? "Brightness saved." : "Could not save brightness. Try again.");
    }
    if (!snapshot.serverBusy && snapshot.serverGeneration != ui.serverGeneration) {
        ui.serverGeneration = snapshot.serverGeneration;
        lv_obj_remove_state(ui.serverSave, LV_STATE_DISABLED);
        if (snapshot.serverGeneration) {
            switch (snapshot.serverError) {
            case ESP_OK: lv_label_set_text(ui.serverStatus, "Saved. The board now uses this PC server."); break;
            case ESP_ERR_INVALID_STATE:
                lv_label_set_text(ui.serverStatus, "Connect to Wi-Fi first. The server is saved with the Wi-Fi settings.");
                break;
            case ESP_ERR_INVALID_ARG:
                lv_label_set_text(ui.serverStatus, "Invalid address. Use an IPv4 address such as 192.168.0.35 or a host name.");
                break;
            case ESP_ERR_TIMEOUT: lv_label_set_text(ui.serverStatus, "Wi-Fi is busy. Try again in a moment."); break;
            default: lv_label_set_text(ui.serverStatus, "Could not save the PC server. Try again."); break;
            }
        }
    }
    if (snapshot.generation != ui.generation) {
        ui.generation = snapshot.generation;
        const bool busy = snapshot.phase == Phase::Scanning || snapshot.phase == Phase::Joining;
        for (lv_obj_t *control : {ui.scan, ui.connect, ui.list, ui.ssid, ui.password}) {
            if (busy) lv_obj_add_state(control, LV_STATE_DISABLED);
            else lv_obj_remove_state(control, LV_STATE_DISABLED);
        }
        switch (snapshot.phase) {
        case Phase::Scanning: lv_label_set_text(ui.status, "Searching for nearby Wi-Fi..."); break;
        case Phase::Joining: lv_label_set_text(ui.status, "Connecting... Waiting for an IP address."); break;
        case Phase::Joined:
            lv_label_set_text(ui.status, "Connected. Wi-Fi settings saved for the next start.");
            lv_textarea_set_password_mode(ui.password, false);
            break;
        case Phase::ScanDone:
            ui.count = snapshot.count;
            std::memcpy(ui.aps, snapshot.aps, sizeof(ui.aps));
            lv_obj_clean(ui.list);
            for (uint16_t i = 0; i < ui.count; ++i) {
                char caption[96];
                std::snprintf(caption, sizeof(caption), "%s\n%d dBm  %s", ui.aps[i].ssid,
                              ui.aps[i].rssi, ui_tr(ui.aps[i].secure ? "Password" : "Open"));
                lv_obj_t *row = lv_list_add_button(ui.list, nullptr, caption);
                lv_obj_set_style_text_font(row, &ard_font_16, 0);
                lv_obj_add_event_cb(row, selectNetwork, LV_EVENT_CLICKED,
                                    reinterpret_cast<void *>(static_cast<uintptr_t>(i + 1)));
            }
            lv_label_set_text(ui.status, ui.count ? "Select a network from the list." : "No networks found. Search again or enter a hidden SSID.");
            break;
        case Phase::Failed:
            if (snapshot.error == ESP_ERR_WIFI_NOT_STARTED)
                lv_label_set_text(ui.status, "Wi-Fi is not ready. Wait a moment and try again.");
            else if (snapshot.action == Action::Scan)
                lv_label_set_text(ui.status, "Wi-Fi search failed. Wait a moment and search again.");
            else
                lv_label_set_text(ui.status, "Connection or save failed. Check the password and retry. Previous settings were kept.");
            break;
        default: break;
        }
    }
    if (!visible) return;
    ard_network_settings_t saved;
    ard_network_get_settings(&saved);
    if (!ui.savedSsidLoaded && saved.version == 1) {
        ui.savedSsidLoaded = true;
        if (!lv_textarea_get_text(ui.ssid)[0]) {
            lv_textarea_set_text(ui.ssid, saved.ssid);
            lv_textarea_set_text(ui.password, saved.password);
        }
        scan(nullptr); // Populate nearby networks automatically when settings first opens.
    }
    // Follow VOL -/+ on the Voice tab, the gamepad and the PC, except while dragging.
    const OnboardAudioStatus audio = onboard_audio_status();
    if (!lv_obj_has_state(ui.volume, LV_STATE_PRESSED) && lv_slider_get_value(ui.volume) != audio.volume) {
        lv_slider_set_value(ui.volume, audio.volume, LV_ANIM_OFF);
        showVolume(audio.volume);
    }
    lv_label_set_text(lv_obj_get_child(ui.mute, 0), audio.muted ? "Unmute" : "Mute");
    if (!audio.ready) lv_label_set_text(ui.soundStatus, "Speaker unavailable (ES8311 not started).");
    else if (audio.muted != ui.soundMuted)
        lv_label_set_text(ui.soundStatus, audio.muted ? "Muted. Press Unmute to hear sound again." : "Sound on.");
    ui.soundMuted = audio.muted;
    if (!ui.serverLoaded && saved.version == 1) {
        ui.serverLoaded = true;
        lv_textarea_set_text(ui.server, saved.server);
    }
    char inUse[65];
    ard_network_server(inUse, sizeof(inUse));
    lv_label_set_text_fmt(ui.serverCurrent, "Saved: %s   |   In use: %s   |   PC: %s", saved.server, inUse,
                          ui_tr(!ard_network_connected() ? "Wi-Fi not connected"
                                : pc_status().reachable ? "responding" : "no response"));
    char ip[16];
    ard_network_local_ip(ip, sizeof(ip));
    if (snapshot.phase == Phase::Joining)
        lv_label_set_text(ui.connection, "Connecting to the selected Wi-Fi...");
    else if (ard_network_connected())
        lv_label_set_text_fmt(ui.connection, "Connected: %s  |  IP: %s", saved.ssid, ip);
    else
        lv_label_set_text(ui.connection, "Not connected  |  2.4 GHz Wi-Fi");
    clearSecret(saved.password, sizeof(saved.password));
}
