#include "onboard_audio.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ard_network.h"
#include "board.h"
#include "voice_capture.h"
#include "cJSON.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"

namespace {
constexpr int kRate = 16000, kFrame = 256, kVolumeMax = 21, kPort = 5005;
constexpr size_t kMaxSamples = kRate * CONFIG_ARD_VOICE_MAX_SECONDS;
constexpr size_t kPreSamples = kRate / 10;
constexpr uint32_t kPeerTimeoutMs = 12000, kEchoTailMs = 500;
// Switching the speaker amplifier off clicks into the microphone; ignore it.
constexpr uint32_t kAmpSettleMs = 600;
const char *TAG = "onboard_audio";

struct Packet { uint32_t generation; uint16_t count; int16_t mono[512]; };
struct Upload { size_t samples; uint32_t selection; };
QueueHandle_t packets, uploads;
i2s_chan_handle_t tx, rx;
esp_codec_dev_handle_t codec;
const audio_codec_data_if_t *dataIf;
const audio_codec_ctrl_if_t *ctrlIf;
const audio_codec_gpio_if_t *gpioIf;
const audio_codec_if_t *chipIf;
TaskHandle_t tasks[3];
int16_t *clip;

std::atomic<bool> ready{false}, micEnabled{true}, selected{false}, vad{true}, ptt{false};
std::atomic<bool> recording{false}, playing{false}, uploading{false}, remoteBusy{false}, muted{false};
std::atomic<bool> dirty{false}, workersGo{false};
std::atomic<int> volume{12}, peak{0}, threshold{2000}, cue{0};
std::atomic<uint32_t> generation{0}, selection{0}, awaitedJob{0}, completedJob{0}, serverAddress{0}, serverSession{0};
std::atomic<uint32_t> rxPackets{0}, dropped{0}, uploaded{0}, uploadErrors{0};
std::atomic<uint32_t> peerSeen{0}, echoUntil{0}, discardUntil{0};
// A local change wins until the PC has had time to store it (status polls every 3 s).
std::atomic<uint32_t> localVolumeUntil{0};
constexpr uint32_t kLocalVolumeHoldMs = 4000;
portMUX_TYPE errorLock = portMUX_INITIALIZER_UNLOCKED;
char lastError[64];

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }
void storeVolume(int value) {
    value = value < 0 ? 0 : value > kVolumeMax ? kVolumeMax : value;
    if (volume.exchange(value) != value) dirty.store(true);
}
bool before(uint32_t deadline) { return (int32_t)(nowMs() - deadline) < 0; }
void error(const char *message) {
    char copy[64]{};
    snprintf(copy, sizeof(copy), "%s", message);
    taskENTER_CRITICAL(&errorLock);
    memcpy(lastError, copy, sizeof(copy));
    taskEXIT_CRITICAL(&errorLock);
    ESP_LOGW(TAG, "%s", message);
}
bool active() {
    return ready.load() && selected.load() && ard_network_connected() && (uint32_t)(nowMs() - peerSeen.load()) < kPeerTimeoutMs;
}
void loadSettings() {
    nvs_handle_t nvs;
    uint8_t v = 12, mic = 1;
    if (nvs_open("jc_audio", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, "volume", &v);
        nvs_get_u8(nvs, "mic", &mic);
        nvs_close(nvs);
    }
    volume.store(v <= kVolumeMax ? v : 12);
    micEnabled.store(mic != 0);
}
void saveSettings() {
    if (!dirty.exchange(false)) return;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("jc_audio", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "volume", volume.load());
        if (err == ESP_OK) err = nvs_set_u8(nvs, "mic", micEnabled.load());
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK) error("Audio setting save failed (runtime value retained)");
}
void destroyCodec() {
    if (codec) { esp_codec_dev_close(codec); esp_codec_dev_delete(codec); codec = nullptr; }
    if (chipIf) { audio_codec_delete_codec_if(chipIf); chipIf = nullptr; }
    if (ctrlIf) { audio_codec_delete_ctrl_if(ctrlIf); ctrlIf = nullptr; }
    if (gpioIf) { audio_codec_delete_gpio_if(gpioIf); gpioIf = nullptr; }
    if (dataIf) { audio_codec_delete_data_if(dataIf); dataIf = nullptr; }
    if (rx) { i2s_channel_disable(rx); i2s_del_channel(rx); rx = nullptr; }
    if (tx) { i2s_channel_disable(tx); i2s_del_channel(tx); tx = nullptr; }
    gpio_set_level(PIN_SPK_AMP_EN, 0);
}
esp_err_t initCodec(i2c_master_bus_handle_t bus) {
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    channel.dma_desc_num = 4;
    channel.dma_frame_num = kFrame;
    esp_err_t err = i2s_new_channel(&channel, &tx, &rx);
    if (err != ESP_OK) return err;
    i2s_std_config_t config{};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRate);
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk = PIN_AUDIO_MCLK;
    config.gpio_cfg.bclk = PIN_AUDIO_BCLK;
    config.gpio_cfg.ws = PIN_AUDIO_WS;
    config.gpio_cfg.dout = PIN_AUDIO_DOUT;
    config.gpio_cfg.din = PIN_AUDIO_DIN;
    if ((err = i2s_channel_init_std_mode(tx, &config)) != ESP_OK ||
        (err = i2s_channel_init_std_mode(rx, &config)) != ESP_OK ||
        (err = i2s_channel_enable(tx)) != ESP_OK || (err = i2s_channel_enable(rx)) != ESP_OK) return err;

    audio_codec_i2s_cfg_t data{};
    data.port = I2S_NUM_0; data.tx_handle = tx; data.rx_handle = rx;
    dataIf = audio_codec_new_i2s_data(&data);
    audio_codec_i2c_cfg_t control{};
    control.port = I2C_NUM_0; control.addr = ES8311_CODEC_DEFAULT_ADDR; control.bus_handle = bus;
    ctrlIf = audio_codec_new_i2c_ctrl(&control);
    gpioIf = audio_codec_new_gpio();
    if (!dataIf || !ctrlIf || !gpioIf) return ESP_ERR_NO_MEM;
    es8311_codec_cfg_t chip{};
    chip.ctrl_if = ctrlIf; chip.gpio_if = gpioIf;
    chip.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    chip.pa_pin = PIN_SPK_AMP_EN; chip.use_mclk = true; chip.mclk_div = 256;
    chip.no_dac_ref = true; // ADC left slot, no DAC loopback supplied as microphone data.
    chip.hw_gain.pa_voltage = 5.0f; chip.hw_gain.codec_dac_voltage = 3.3f;
    chipIf = es8311_codec_new(&chip);
    if (!chipIf) return ESP_FAIL;
    esp_codec_dev_cfg_t device{};
    device.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT; device.codec_if = chipIf; device.data_if = dataIf;
    codec = esp_codec_dev_new(&device);
    if (!codec) return ESP_ERR_NO_MEM;
    esp_codec_dev_sample_info_t format{};
    format.sample_rate = kRate; format.channel = 2; format.bits_per_sample = 16;
    if (esp_codec_dev_open(codec, &format) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_vol(codec, 80) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_in_gain(codec, CONFIG_ARD_VOICE_MIC_GAIN_DB) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    gpio_set_level(PIN_SPK_AMP_EN, 0);
    return ESP_OK;
}

// One task owns all codec reads/writes and the capture buffer. No LVGL or network I/O here.
void ioTask(void *) {
    while (!workersGo.load()) vTaskDelay(1);
    const bool watched = esp_task_wdt_add(nullptr) == ESP_OK;
    static int16_t input[kFrame * 2], output[kFrame * 2], mono[kFrame], pre[kPreSamples];
    static int16_t sine[128];
    for (int i = 0; i < 128; ++i) sine[i] = (int16_t)(std::sin(i * 6.28318530718 / 128) * 5000);
    Packet packet{};
    voice_capture_t capture{};
    size_t preAt = 0, preCount = 0, count = 0, packetAt = 0;
    uint32_t phase = 0, toneRemaining = 0, lastGeneration = generation.load();
    int melody = 0, note = 0, toneHz = 0;
    bool lastVad = vad.load(), ampOn = false;
    uint32_t micHoldUntil = 0;
    // Idle peak level of the room (fans, motors). VAD speech must clearly exceed it,
    // in addition to the PC's sensitivity threshold.
    int noiseFloor = 0;
    const int notes[] = {523, 659, 784, 988, 1175, 1568};
    const int lengths[] = {100, 100, 100, 100, 120, 300};
    for (;;) {
        if (watched) esp_task_wdt_reset();
        size_t bytes = 0;
        esp_err_t err = i2s_channel_read(rx, input, sizeof(input), &bytes, 100);
        if (err != ESP_OK || bytes != sizeof(input)) {
            ready.store(false); recording.store(false); playing.store(false);
            gpio_set_level(PIN_SPK_AMP_EN, 0);
            error("I2S microphone read failed; audio disabled, motion remains available");
            break;
        }
        int maximum = 0;
        for (int i = 0; i < kFrame; ++i) {
            mono[i] = input[i * 2 + CONFIG_ARD_VOICE_MIC_SLOT];
            const int v = mono[i] < 0 ? -(int)mono[i] : mono[i];
            if (v > maximum) maximum = v;
        }
        peak.store(maximum);

        if (lastGeneration != generation.load()) {
            lastGeneration = generation.load(); melody = 0; toneRemaining = 0; packet.count = 0; packetAt = 0;
        }
        const int requestedCue = cue.exchange(0);
        if (requestedCue) { melody = requestedCue; note = 0; toneRemaining = 0; }
        if (melody && !toneRemaining) {
            if (note == (melody == 1 ? 6 : 3)) melody = 0;
            else {
                toneHz = notes[note];
                toneRemaining = kRate * lengths[note] / 1000;
                ++note;
            }
        }
        const bool mode = vad.load();
        const bool permitted = active() && micEnabled.load() && !uploading.load() &&
            !remoteBusy.load() && !awaitedJob.load() && !before(echoUntil.load()) && !before(micHoldUntil) &&
            !melody && mode == lastVad;
        lastVad = mode;
        const int floorLevel = noiseFloor * 3;
        const int speechLevel = threshold.load() > floorLevel ? threshold.load() : floorLevel;
        const bool speech = maximum > speechLevel;
        // Follow only non-speech frames, slowly (about 4 s), so speech never raises it.
        if (permitted && !capture.recording && !speech) noiseFloor += (maximum - noiseFloor) / 256;
        const voice_capture_event_t event = voice_capture_step(&capture, permitted, mode, ptt.load(),
            speech, kFrame, (uint32_t)kMaxSamples - kPreSamples, kRate * 3 / 4);
        if (event == VOICE_START) {
            count = 0;
            for (size_t i = 0; i < preCount; ++i) clip[count++] = pre[(preAt + kPreSamples - preCount + i) % kPreSamples];
        }
        if (event == VOICE_START || event == VOICE_KEEP || event == VOICE_FINISH) {
            if (count + kFrame <= kMaxSamples) {
                memcpy(clip + count, mono, sizeof(mono)); count += kFrame;
            }
        }
        if (event == VOICE_CANCEL) count = 0;
        if (event == VOICE_FINISH) ptt.store(false);
        if (event == VOICE_FINISH && count >= kRate / 10 && capture.samples >= kRate / 10) {
            Upload job{count, selection.load()};
            uploading.store(true);
            if (xQueueSend(uploads, &job, 0) != pdTRUE) { uploading.store(false); ++uploadErrors; }
            count = 0;
        }
        recording.store(capture.recording);
        if (permitted && !capture.recording) {
            for (int i = 0; i < kFrame; ++i) { pre[preAt] = mono[i]; preAt = (preAt + 1) % kPreSamples; }
            preCount = preCount + kFrame > kPreSamples ? kPreSamples : preCount + kFrame;
        } else if (!permitted) { preCount = 0; }

        bool audible = false;
        const bool quiet = muted.load() || volume.load() == 0;
        for (int i = 0; i < kFrame; ++i) {
            int sample = 0;
            if (toneRemaining) {
                phase += (uint32_t)((uint64_t)toneHz * 4294967296ULL / kRate);
                sample = sine[phase >> 25];
                --toneRemaining;
            } else {
                if (packet.generation != generation.load()) packetAt = packet.count;
                if (packetAt >= packet.count) {
                    if (xQueueReceive(packets, &packet, 0) == pdTRUE) packetAt = 0;
                    else packet.count = 0;
                }
                if (packetAt < packet.count && packet.generation == generation.load()) sample = packet.mono[packetAt++];
            }
            sample = quiet ? 0 : sample * volume.load() / kVolumeMax;
            output[i * 2] = output[i * 2 + 1] = (int16_t)sample;
            audible |= sample != 0;
        }
        if (audible) echoUntil.store(nowMs() + kEchoTailMs);
        playing.store(audible || before(echoUntil.load()));
        const bool amp = playing.load() && !quiet;
        if (ampOn && !amp) micHoldUntil = nowMs() + kAmpSettleMs;
        ampOn = amp;
        gpio_set_level(PIN_SPK_AMP_EN, amp ? 1 : 0);
        size_t written = 0;
        err = i2s_channel_write(tx, output, sizeof(output), &written, 100);
        if (err != ESP_OK || written != sizeof(output)) {
            ready.store(false); recording.store(false); playing.store(false);
            gpio_set_level(PIN_SPK_AMP_EN, 0);
            error("I2S speaker write failed; audio disabled, motion remains available");
            break;
        }
    }
    if (watched) esp_task_wdt_delete(nullptr);
    vTaskDelete(nullptr);
}

void receiverTask(void *) {
    while (!workersGo.load()) vTaskDelay(1);
    int sock = -1;
    for (;;) {
        if (sock < 0) {
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in local{}; local.sin_family = AF_INET; local.sin_port = htons(kPort);
            const timeval timeout{0, 200000};
            if (sock < 0 || setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
                bind(sock, (sockaddr *)&local, sizeof(local))) {
                if (sock >= 0) close(sock);
                sock = -1; vTaskDelay(pdMS_TO_TICKS(1000)); continue;
            }
        }
        Packet packet{};
        uint8_t wire[1025]; // one extra byte detects oversized UDP datagrams before PCM conversion.
        sockaddr_in peer{}; socklen_t peerSize = sizeof(peer);
        const int n = recvfrom(sock, wire, sizeof(wire), 0, (sockaddr *)&peer, &peerSize);
        if (n <= 0) {
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) { close(sock); sock = -1; }
            continue;
        }
        if (!active() || peer.sin_addr.s_addr != serverAddress.load()) continue;
        if (n >= 5 && n <= 12 && !memcmp(wire, "vol=", 4)) {
            char command[16]{}; memcpy(command, wire, n);
            onboard_audio_command(command); continue;
        }
        if (n > 1024 || n % 2 || muted.load() || before(discardUntil.load())) { ++dropped; continue; }
        packet.generation = generation.load(); packet.count = (uint16_t)(n / 2);
        memcpy(packet.mono, wire, n);
        ++rxPackets;
        if (xQueueSend(packets, &packet, 0) != pdTRUE) ++dropped;
    }
}

void uploadTask(void *) {
    while (!workersGo.load()) vTaskDelay(1);
    for (;;) {
        saveSettings();
        Upload job{};
        if (xQueueReceive(uploads, &job, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        bool accepted = false;
        if (active() && job.selection == selection.load()) {
            char host[65], url[160], response[256]{};
            ard_network_server(host, sizeof(host));
            snprintf(url, sizeof(url), "http://%s:%d/api/jc1060/voice", host, ARD_PC_HTTP_PORT);
            esp_http_client_config_t config{};
            config.url = url; config.method = HTTP_METHOD_POST; config.timeout_ms = 5000;
            config.disable_auto_redirect = true; config.buffer_size = 1024;
            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (client) {
                const int length = (int)(job.samples * sizeof(int16_t));
                esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
                esp_http_client_set_header(client, "X-Audio-Format", "pcm_s16le;rate=16000;channels=1");
                esp_err_t err = esp_http_client_open(client, length);
                int sent = 0;
                while (err == ESP_OK && sent < length && active() && job.selection == selection.load()) {
                    const int n = esp_http_client_write(client, (const char *)clip + sent, length - sent);
                    if (n <= 0) { err = ESP_FAIL; break; }
                    sent += n;
                }
                if (err == ESP_OK && sent == length && esp_http_client_fetch_headers(client) >= 0 &&
                    esp_http_client_get_status_code(client) == 202) {
                    const int n = esp_http_client_read_response(client, response, sizeof(response) - 1);
                    if (n > 0) {
                        cJSON *doc = cJSON_Parse(response);
                        const cJSON *id = cJSON_GetObjectItem(doc, "job_id");
                        if (cJSON_IsNumber(id) && id->valuedouble > 0 && id->valuedouble <= UINT32_MAX) {
                            const uint32_t jobId = (uint32_t)id->valuedouble;
                            if (job.selection == selection.load() && completedJob.load() < jobId) awaitedJob.store(jobId);
                            accepted = true; ++uploaded;
                        }
                        cJSON_Delete(doc);
                    }
                }
                esp_http_client_close(client); esp_http_client_cleanup(client);
            }
        }
        if (!accepted) { ++uploadErrors; error("Voice upload failed/busy; recording was not retried"); }
        uploading.store(false); // release the sole clip buffer only after the HTTP client is done.
    }
}
} // namespace

esp_err_t onboard_audio_start(i2c_master_bus_handle_t bus) {
    if (!bus || workersGo.load()) return ESP_ERR_INVALID_STATE;
    loadSettings();
    packets = xQueueCreate(16, sizeof(Packet)); uploads = xQueueCreate(1, sizeof(Upload));
    clip = (int16_t *)heap_caps_malloc(kMaxSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_err_t err = packets && uploads && clip ? initCodec(bus) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        const TaskFunction_t functions[] = {ioTask, receiverTask, uploadTask};
        const char *names[] = {"voice_io", "voice_rx", "voice_upload"};
        const uint32_t stacks[] = {6144, 4096, 6144};
        const UBaseType_t priorities[] = {6, 5, 3};
        for (int i = 0; i < 3; ++i) {
            if (xTaskCreatePinnedToCore(functions[i], names[i], stacks[i], nullptr, priorities[i], &tasks[i], 0) != pdPASS) {
                err = ESP_ERR_NO_MEM; break;
            }
        }
    }
    if (err != ESP_OK) {
        for (auto &task : tasks) if (task) { vTaskDelete(task); task = nullptr; }
        destroyCodec();
        if (packets) { vQueueDelete(packets); packets = nullptr; }
        if (uploads) { vQueueDelete(uploads); uploads = nullptr; }
        heap_caps_free(clip); clip = nullptr;
        error("ES8311 unavailable; onboard microphone/speaker disabled");
        return err;
    }
    ready.store(true); workersGo.store(true);
    ESP_LOGI(TAG, "ES8311 16 kHz full duplex, PCM buffer %u bytes PSRAM; mic slot %d",
             (unsigned)(kMaxSamples * 2), CONFIG_ARD_VOICE_MIC_SLOT);
    return ESP_OK;
}
OnboardAudioStatus onboard_audio_status() {
    OnboardAudioStatus s{};
    s.ready = ready.load(); s.micEnabled = micEnabled.load(); s.selected = active(); s.vad = vad.load();
    s.recording = recording.load(); s.playing = playing.load(); s.uploading = uploading.load();
    s.waiting = awaitedJob.load() != 0 || remoteBusy.load(); s.muted = muted.load(); s.volume = volume.load();
    s.peak = peak.load(); s.rxPackets = rxPackets.load(); s.droppedPackets = dropped.load();
    s.uploadedClips = uploaded.load(); s.uploadErrors = uploadErrors.load();
    taskENTER_CRITICAL(&errorLock); memcpy(s.error, lastError, sizeof(s.error)); taskEXIT_CRITICAL(&errorLock);
    return s;
}
void onboard_audio_pc_state(bool enabled, bool mode, bool busy, uint32_t completed, uint32_t session,
                            int vol, int sensitivity, const char *serverIp) {
    const uint32_t address = inet_addr(serverIp);
    if (address == INADDR_NONE || address == INADDR_ANY) enabled = false;
    const bool changed = selected.load() != enabled || serverAddress.load() != address || serverSession.load() != session;
    if (changed) { ++selection; awaitedJob.store(0); ptt.store(false); }
    selected.store(enabled); serverAddress.store(address); serverSession.store(session); peerSeen.store(nowMs());
    completedJob.store(completed); remoteBusy.store(enabled && busy); vad.store(mode);
    if (awaitedJob.load() && completed >= awaitedJob.load()) awaitedJob.store(0);
    if (enabled && vol >= 0 && vol <= kVolumeMax && !before(localVolumeUntil.load())) storeVolume(vol);
    threshold.store(sensitivity < 100 ? 100 : sensitivity > 32767 ? 32767 : sensitivity);
}
void onboard_audio_set_ptt(bool held) { ptt.store(held); }
void onboard_audio_set_mic(bool enabled) {
    micEnabled.store(enabled);
    if (!enabled) ptt.store(false);
    dirty.store(true);
}
void onboard_audio_set_volume(int value) {
    localVolumeUntil.store(nowMs() + kLocalVolumeHoldMs);
    storeVolume(value);
}
void onboard_audio_stop() {
    ++generation; cue.store(0); discardUntil.store(nowMs() + 500);
    if (packets) xQueueReset(packets);
}
void onboard_audio_boot() { if (ready.load()) cue.store(1); }
void onboard_audio_test() { if (ready.load()) cue.store(2); }
bool onboard_audio_is_command(const char *s) {
    return !strncmp(s, "vol=", 4) || !strncmp(s, "volume=", 7) || !strcmp(s, "vol+") || !strcmp(s, "vol-") ||
        !strcmp(s, "mute") || !strcmp(s, "unmute") || !strcmp(s, "play") || !strcmp(s, "vad") || !strcmp(s, "ptt") ||
        !strncmp(s, "audio_", 6) || !strcmp(s, "mic_ready") || !strcmp(s, "mic_done");
}
bool onboard_audio_command(const char *s) {
    if (!strcmp(s, "audio_status")) {
        const auto state = onboard_audio_status();
        printf("Audio ready=%d mic=%d selected=%d recording=%d playing=%d volume=%d peak=%d clips=%lu errors=%lu\n",
               state.ready, state.micEnabled, state.selected, state.recording, state.playing, state.volume, state.peak,
               (unsigned long)state.uploadedClips, (unsigned long)state.uploadErrors);
        return true;
    }
    if (!ready.load()) return false;
    if (!strncmp(s, "vol=", 4) || !strncmp(s, "volume=", 7)) {
        const char *value = strchr(s, '=') + 1;
        if (!*value) return false;
        for (const char *p = value; *p; ++p) if (*p < '0' || *p > '9') return false;
        char *end; const long v = strtol(value, &end, 10);
        if (*end || v < 0 || v > kVolumeMax) return false;
        onboard_audio_set_volume((int)v);
    } else if (!strcmp(s, "vol+") || !strcmp(s, "vol-")) onboard_audio_set_volume(volume.load() + (s[3] == '+' ? 1 : -1));
    else if (!strcmp(s, "mute")) { muted.store(true); onboard_audio_stop(); }
    else if (!strcmp(s, "play") || !strcmp(s, "unmute")) muted.store(false);
    else if (!strcmp(s, "vad") || !strcmp(s, "ptt")) { vad.store(!strcmp(s, "vad")); ptt.store(false); }
    else if (!strcmp(s, "audio_stop")) onboard_audio_stop();
    else if (!strcmp(s, "audio_test")) onboard_audio_test();
    else if (!strcmp(s, "audio_mic_on")) onboard_audio_set_mic(true);
    else if (!strcmp(s, "audio_mic_off")) onboard_audio_set_mic(false);
    else if (!strcmp(s, "mic_ready")) onboard_audio_set_ptt(true);
    else if (!strcmp(s, "mic_done")) onboard_audio_set_ptt(false);
    else return false;
    return true;
}
