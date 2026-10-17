#include "gamepad.h"
#include "sdkconfig.h"

#if CONFIG_ARD_ENABLE_GAMEPAD
#include <stdio.h>
#include <string.h>
#include <atomic>
#include "esp_hidh.h"
extern "C" {
#include "esp_hosted_misc.h" // no C++ guards in ESP-Hosted 2.12.2
}
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"

extern "C" void ble_store_config_init(void);

static const char *TAG = "gamepad";

namespace {

constexpr uint16_t kHidService = 0x1812;
constexpr uint32_t kScanMs = 4000;
constexpr uint32_t kPairWindowMs = 60000;

struct Candidate { bool found; ble_addr_t addr; int8_t rssi; char name[32]; };

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
GamepadState state;
GamepadInfo info = {GAMEPAD_OFF, "", "", 0, 0, -1};
uint32_t lastReport = 0;
HidGamepadParser parser;

std::atomic<bool> synced{false}, connected{false}, scanning{false};
std::atomic<uint32_t> pairUntil{0};
SemaphoreHandle_t scanDone;
Candidate candidate;
bool haveSaved = false;
ble_addr_t saved;
esp_hidh_dev_t *device;

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

void setLink(GamepadLink link) {
    portENTER_CRITICAL(&lock);
    info.link = link;
    portEXIT_CRITICAL(&lock);
}

void formatAddr(const uint8_t *val, char *out, size_t size) {
    snprintf(out, size, "%02X:%02X:%02X:%02X:%02X:%02X", val[5], val[4], val[3], val[2], val[1], val[0]);
}

void loadSaved() {
    nvs_handle_t nvs;
    if (nvs_open("ard-gamepad", NVS_READONLY, &nvs) != ESP_OK) return;
    size_t size = sizeof(saved);
    haveSaved = nvs_get_blob(nvs, "addr", &saved, &size) == ESP_OK && size == sizeof(saved);
    nvs_close(nvs);
}

void storeSaved(const ble_addr_t *addr) {
    nvs_handle_t nvs;
    if (nvs_open("ard-gamepad", NVS_READWRITE, &nvs) != ESP_OK) return;
    if (addr) nvs_set_blob(nvs, "addr", addr, sizeof(*addr)); else nvs_erase_key(nvs, "addr");
    nvs_commit(nvs);
    nvs_close(nvs);
    haveSaved = addr != nullptr;
    if (addr) saved = *addr;
}

bool pairing() { return !haveSaved || nowMs() < pairUntil.load(); }

int gapEvent(ble_gap_event *event, void *) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) return 0;
        bool hid = false;
        for (int i = 0; i < fields.num_uuids16; ++i)
            if (ble_uuid_u16(&fields.uuids16[i].u) == kHidService) hid = true;
        const bool known = haveSaved && !ble_addr_cmp(&event->disc.addr, &saved);
        // Keyboards (0x3C1) and mice (0x3C2) are never taken as a gamepad.
        const uint16_t appearance = fields.appearance_is_present ? fields.appearance : 0;
        const bool padLike = appearance == 0 || appearance == 0x3C0 || appearance == 0x3C3 || appearance == 0x3C4;
        if (!known && !(pairing() && hid && padLike)) return 0;
        if (!known && candidate.found && event->disc.rssi <= candidate.rssi) return 0;
        candidate.found = true;
        candidate.addr = event->disc.addr;
        candidate.rssi = event->disc.rssi;
        candidate.name[0] = 0;
        if (fields.name && fields.name_len) {
            const size_t n = fields.name_len < sizeof(candidate.name) - 1 ? fields.name_len : sizeof(candidate.name) - 1;
            memcpy(candidate.name, fields.name, n);
            candidate.name[n] = 0;
        }
        if (known) ble_gap_disc_cancel(); // nothing better to wait for
        if (known) xSemaphoreGive(scanDone);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        xSemaphoreGive(scanDone);
    }
    return 0;
}

// Short supervision timeout: a pad that goes out of range is detected in ~0.6 s, which stops the jog.
void tightenConnection(const uint8_t *bda) {
    ble_addr_t addr = candidate.addr;
    if (memcmp(addr.val, bda, 6)) return;
    ble_gap_conn_desc desc;
    if (ble_gap_conn_find_by_addr(&addr, &desc) != 0) return;
    ble_gap_upd_params params = {};
    params.itvl_min = 6;          // 7.5 ms
    params.itvl_max = 12;         // 15 ms
    params.latency = 0;
    params.supervision_timeout = 60; // 600 ms
    const int rc = ble_gap_update_params(desc.conn_handle, &params);
    if (rc) ESP_LOGW(TAG, "conn param update rc=%d (pad keeps its own timing)", rc);
}

void hidEvent(void *, esp_event_base_t, int32_t id, void *data) {
    auto *p = static_cast<esp_hidh_event_data_t *>(data);
    switch ((esp_hidh_event_t)id) {
    case ESP_HIDH_OPEN_EVENT: {
        if (!p->open.dev) break;
        device = p->open.dev;
        size_t maps = 0;
        esp_hid_raw_report_map_t *raw = nullptr;
        int fields = 0;
        if (esp_hidh_dev_report_maps_get(device, &maps, &raw) == ESP_OK)
            for (size_t i = 0; i < maps && !fields; ++i) fields = parser.parse(raw[i].data, raw[i].len);
        const uint8_t *bda = esp_hidh_dev_bda_get(device);
        portENTER_CRITICAL(&lock);
        state = GamepadState();
        info.link = GAMEPAD_CONNECTED;
        info.reports = 0;
        info.battery = -1;
        strlcpy(info.name, esp_hidh_dev_name_get(device) ? esp_hidh_dev_name_get(device) : "", sizeof(info.name));
        if (bda) formatAddr(bda, info.address, sizeof(info.address));
        lastReport = nowMs();
        portEXIT_CRITICAL(&lock);
        connected = true;
        ESP_LOGI(TAG, "Connected %s (%s), %d gamepad fields", info.name, info.address, fields);
        if (!fields) ESP_LOGW(TAG, "Report map has no gamepad axes/buttons; inputs will be ignored");
        if (bda) {
            if (!haveSaved || memcmp(saved.val, bda, 6)) storeSaved(&candidate.addr);
            pairUntil = 0;
            tightenConnection(bda);
        }
        break;
    }
    case ESP_HIDH_INPUT_EVENT: {
        portENTER_CRITICAL(&lock);
        if (parser.decode((uint8_t)p->input.report_id, p->input.data, p->input.length, state)) {
            ++info.reports;
            lastReport = nowMs();
        }
        portEXIT_CRITICAL(&lock);
        break;
    }
    case ESP_HIDH_BATTERY_EVENT:
        portENTER_CRITICAL(&lock);
        info.battery = p->battery.level;
        portEXIT_CRITICAL(&lock);
        break;
    case ESP_HIDH_CLOSE_EVENT:
        connected = false;
        device = nullptr;
        portENTER_CRITICAL(&lock);
        state = GamepadState(); // neutral sticks: the jog stops
        info.link = GAMEPAD_SEARCHING;
        portEXIT_CRITICAL(&lock);
        ESP_LOGW(TAG, "Gamepad disconnected (reason %d)", p->close.reason);
        break;
    default:
        break;
    }
}

void onSync() {
    ble_hs_util_ensure_addr(0);
    synced = true;
}

void hostTask(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void managerTask(void *) {
    while (!synced) vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "BLE host ready; %s", haveSaved ? "reconnecting to the saved pad" : "pairing: put the pad in pairing mode");
    for (;;) {
        if (connected) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
        setLink(pairing() ? GAMEPAD_PAIRING : GAMEPAD_SEARCHING);
        candidate = {};
        uint8_t own;
        ble_hs_id_infer_auto(0, &own);
        ble_gap_disc_params params = {};
        params.passive = 0;          // active scan: names come in scan responses
        params.filter_duplicates = 1;
        params.itvl = 0x50;
        params.window = 0x30;
        xSemaphoreTake(scanDone, 0);
        if (ble_gap_disc(own, kScanMs, &params, gapEvent, nullptr) != 0) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        xSemaphoreTake(scanDone, pdMS_TO_TICKS(kScanMs + 1000));
        if (!candidate.found) continue;
        setLink(GAMEPAD_CONNECTING);
        ESP_LOGI(TAG, "Connecting to %s rssi %d", candidate.name[0] ? candidate.name : "(no name)", candidate.rssi);
        // Blocks until the GATT discovery finishes (or fails); OPEN arrives through hidEvent.
        if (!esp_hidh_dev_open(candidate.addr.val, ESP_HID_TRANSPORT_BLE, candidate.addr.type)) {
            ESP_LOGW(TAG, "Open failed; retrying");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

} // namespace

esp_err_t gamepad_start() {
    scanDone = xSemaphoreCreateBinary();
    if (!scanDone) return ESP_ERR_NO_MEM;
    loadSaved();
    // Wi-Fi already brought ESP-Hosted up; these switch on the C6 Bluetooth controller (VHCI).
    esp_err_t err = esp_hosted_bt_controller_init();
    if (err == ESP_OK) err = esp_hosted_bt_controller_enable();
    if (err != ESP_OK) { ESP_LOGE(TAG, "C6 BT controller: %s", esp_err_to_name(err)); return err; }
    if ((err = nimble_port_init()) != ESP_OK) return err;
    ble_hs_cfg.sync_cb = onSync;
    esp_hidh_config_t config = {};
    config.callback = hidEvent;
    config.event_stack_size = 4096;
    if ((err = esp_hidh_init(&config)) != ESP_OK) return err;
    // Bond with "just works" pairing; keys are kept so the pad reconnects after a reboot.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    nimble_port_freertos_init(hostTask);
    return xTaskCreatePinnedToCore(managerTask, "gamepad", 4096, nullptr, 3, nullptr, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool gamepad_state(GamepadState *out) {
    if (!connected) return false;
    portENTER_CRITICAL(&lock);
    *out = state;
    portEXIT_CRITICAL(&lock);
    return true;
}

GamepadInfo gamepad_info() {
    portENTER_CRITICAL(&lock);
    GamepadInfo copy = info;
    copy.ageMs = nowMs() - lastReport;
    portEXIT_CRITICAL(&lock);
    return copy;
}

void gamepad_pair() { pairUntil = nowMs() + kPairWindowMs; }

void gamepad_forget() {
    if (device) esp_hidh_dev_close(device);
    if (haveSaved) ble_store_util_delete_peer(&saved);
    storeSaved(nullptr);
}

#else // gamepad disabled in menuconfig

esp_err_t gamepad_start() { return ESP_OK; }
bool gamepad_state(GamepadState *) { return false; }
GamepadInfo gamepad_info() { return {GAMEPAD_OFF, "", "", 0, 0, -1}; }
void gamepad_pair() {}
void gamepad_forget() {}

#endif
