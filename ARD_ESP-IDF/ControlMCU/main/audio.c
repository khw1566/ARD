// PCM output path. The PC streams 16 kHz mono s16le in 1024-byte (32 ms) UDP
// packets: TTS replies, beeps, radio, music and PC sound mirroring all arrive
// this way. The receiver only queues; one playback task owns the I2S channel and
// applies the volume, so tones and the stream never interleave mid-buffer.
#include "audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "sdkconfig.h"

#define BUFFER_BYTES    16384 // ~0.5 s of audio
#define CHUNK_SAMPLES   256   // 16 ms per I2S write
#define IDLE_AFTER_MS   1500  // same stream timeout as the Arduino build
#define DISCARD_MS      500
#define PREBUFFER_BYTES (AUDIO_SAMPLE_RATE * 2 * CONFIG_CONTROL_PREBUFFER_MS / 1000)

static const char *TAG = "AUDIO";
static i2s_chan_handle_t tx;
static StreamBufferHandle_t queue;
static SemaphoreHandle_t feed_lock;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static audio_status_t status;
static volatile bool flush_requested;
static int64_t discard_until_us, last_data_us;

static int load_volume(void) {
    nvs_handle_t nvs;
    uint8_t v = CONFIG_CONTROL_DEFAULT_VOLUME;
    if (nvs_open("control", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, "volume", &v);
        nvs_close(nvs);
    }
    return v > AUDIO_VOLUME_MAX ? CONFIG_CONTROL_DEFAULT_VOLUME : v;
}

int audio_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > AUDIO_VOLUME_MAX) volume = AUDIO_VOLUME_MAX;
    taskENTER_CRITICAL(&lock);
    bool same = status.volume == volume;
    status.volume = volume;
    taskEXIT_CRITICAL(&lock);
    if (!same) {
        // The Arduino build went back to 12 on every reboot.
        nvs_handle_t nvs;
        if (nvs_open("control", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_u8(nvs, "volume", (uint8_t)volume);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
        ESP_LOGI(TAG, "Volume %d", volume);
    }
    return volume;
}

void audio_stop(void) {
    taskENTER_CRITICAL(&lock);
    discard_until_us = esp_timer_get_time() + DISCARD_MS * 1000LL;
    taskEXIT_CRITICAL(&lock);
    flush_requested = true;
}

void audio_set_muted(bool muted) {
    taskENTER_CRITICAL(&lock);
    status.muted = muted;
    taskEXIT_CRITICAL(&lock);
    if (muted) audio_stop();
    // Shutting the amplifier down also removes its idle hiss.
    gpio_set_level(PIN_AMP_SD, !muted);
    ESP_LOGI(TAG, "%s", muted ? "Muted" : "Unmuted");
}

void audio_get_status(audio_status_t *out) {
    taskENTER_CRITICAL(&lock);
    *out = status;
    taskEXIT_CRITICAL(&lock);
}

// Returns false when the data was not queued (muted, or discarded after a stop).
static bool feed(const void *data, size_t len, TickType_t wait) {
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&lock);
    bool skip = status.muted || now < discard_until_us;
    taskEXIT_CRITICAL(&lock);
    if (skip) return false;
    // A stream buffer allows one writer at a time: the UDP receiver and tone callers share it.
    xSemaphoreTake(feed_lock, portMAX_DELAY);
    size_t sent = xStreamBufferSend(queue, data, len, wait);
    xSemaphoreGive(feed_lock);
    if (sent < len) {
        taskENTER_CRITICAL(&lock);
        status.dropped += len - sent;
        taskEXIT_CRITICAL(&lock);
    }
    return true;
}

void audio_tone(int freq_hz, int ms) {
    int16_t buf[CHUNK_SAMPLES];
    int total = AUDIO_SAMPLE_RATE * ms / 1000, done = 0;
    int fade = AUDIO_SAMPLE_RATE / 200; // 5 ms fade in/out so tones do not click
    while (done < total) {
        int n = total - done < CHUNK_SAMPLES ? total - done : CHUNK_SAMPLES;
        for (int i = 0; i < n; ++i) {
            int k = done + i;
            float env = k < fade ? (float)k / fade : total - k < fade ? (float)(total - k) / fade : 1.0f;
            buf[i] = (int16_t)(9000.0f * env * sinf(2.0f * (float)M_PI * freq_hz * k / AUDIO_SAMPLE_RATE));
        }
        if (!feed(buf, n * 2, portMAX_DELAY)) return;
        done += n;
    }
}

void audio_boot_melody(void) {
    // C major 9 arpeggio, same notes as the Arduino build.
    static const int notes[] = {523, 659, 784, 988, 1175, 1568};
    static const int lengths[] = {100, 100, 100, 100, 120, 300};
    static const int16_t gap[AUDIO_SAMPLE_RATE / 100]; // 10 ms of silence
    for (int i = 0; i < 6; ++i) {
        audio_tone(notes[i], lengths[i]);
        feed(gap, sizeof(gap), portMAX_DELAY);
    }
}

static void playback_task(void *arg) {
    int16_t buf[CHUNK_SAMPLES];
    bool active = false;
    int64_t waiting_since = 0;
    while (true) {
        if (flush_requested) {
            flush_requested = false;
            while (xStreamBufferReceive(queue, buf, sizeof(buf), 0) > 0) {
            }
            active = false;
        }
        if (!active) {
            // Wait for the jitter buffer to fill, or stop waiting after twice its length
            // (a short beep never fills it).
            size_t have = xStreamBufferBytesAvailable(queue);
            if (have == 0) {
                waiting_since = 0;
                vTaskDelay(pdMS_TO_TICKS(4));
                continue;
            }
            int64_t now = esp_timer_get_time();
            if (!waiting_since) waiting_since = now;
            if (have < PREBUFFER_BYTES && now - waiting_since < 2000LL * CONFIG_CONTROL_PREBUFFER_MS) {
                vTaskDelay(pdMS_TO_TICKS(4));
                continue;
            }
            active = true;
            waiting_since = 0;
        }
        size_t got = xStreamBufferReceive(queue, buf, sizeof(buf), pdMS_TO_TICKS(40));
        if (got == 0) {
            // Ran dry: the DMA plays silence (auto_clear) and the jitter buffer refills first.
            active = false;
            taskENTER_CRITICAL(&lock);
            if (status.playing) status.underruns++;
            taskEXIT_CRITICAL(&lock);
            continue;
        }
        size_t n = got / 2;
        taskENTER_CRITICAL(&lock);
        int32_t gain = status.volume * 32767 / AUDIO_VOLUME_MAX;
        taskEXIT_CRITICAL(&lock);
        for (size_t i = 0; i < n; ++i) buf[i] = (int16_t)((buf[i] * gain) >> 15);
        size_t written;
        i2s_channel_write(tx, buf, n * 2, &written, pdMS_TO_TICKS(200));
    }
}

static void handle_control_packet(const uint8_t *pkt, int len) {
    char text[20];
    int n = len < (int)sizeof(text) - 1 ? len : (int)sizeof(text) - 1;
    memcpy(text, pkt, n);
    text[n] = 0;
    // "vol=12" (current PC) or "VOL:12" (older PC builds)
    if ((!strncmp(text, "vol=", 4) || !strncmp(text, "VOL:", 4)) && text[4] >= '0' && text[4] <= '9') {
        int v = atoi(text + 4);
        if (v <= AUDIO_VOLUME_MAX) audio_set_volume(v);
    }
}

static void receiver_task(void *arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(AUDIO_UDP_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "UDP %d bind failed", AUDIO_UDP_PORT);
        vTaskDelete(NULL);
    }
    struct timeval tv = {.tv_sec = 0, .tv_usec = 250 * 1000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ESP_LOGI(TAG, "Voice receiver on UDP %d", AUDIO_UDP_PORT);
    static uint8_t pkt[1472] __attribute__((aligned(4)));
    uint32_t stream_packets = 0, stream_bytes = 0;
    while (true) {
        int len = recv(sock, pkt, sizeof(pkt), 0);
        int64_t now = esp_timer_get_time();
        if (len > 0 && len < 20) {
            handle_control_packet(pkt, len); // never played
        } else if (len >= 20) {
            len &= ~1; // whole 16-bit samples only
            bool queued = feed(pkt, len, 0);
            taskENTER_CRITICAL(&lock);
            bool started = queued && !status.playing;
            if (queued) {
                status.playing = true;
                status.packets++;
                last_data_us = now;
            }
            taskEXIT_CRITICAL(&lock);
            if (started) {
                stream_packets = stream_bytes = 0;
                ESP_LOGI(TAG, "Voice stream started");
            }
            if (queued) {
                stream_packets++;
                stream_bytes += len;
            }
        }
        taskENTER_CRITICAL(&lock);
        bool ended = status.playing && now - last_data_us > IDLE_AFTER_MS * 1000LL;
        if (ended) status.playing = false;
        taskEXIT_CRITICAL(&lock);
        if (ended)
            ESP_LOGI(TAG, "Voice stream ended: %lu packets, %lu bytes", (unsigned long)stream_packets,
                     (unsigned long)stream_bytes);
    }
}

void audio_start(void) {
    status.volume = load_volume();
    gpio_config_t sd = {.pin_bit_mask = 1ULL << PIN_AMP_SD, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&sd));
    gpio_set_level(PIN_AMP_SD, 1);

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 4;
    chan.dma_frame_num = CHUNK_SAMPLES;
    chan.auto_clear = true; // silence instead of a repeating DMA buffer on underrun
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &tx, NULL));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                     .bclk = PIN_I2S_BCLK,
                     .ws = PIN_I2S_WS,
                     .dout = PIN_I2S_DOUT,
                     .din = I2S_GPIO_UNUSED},
    };
    // Same sample on both slots: the amplifier picks L, R or (L+R)/2 from its SD level.
    std.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));

    queue = xStreamBufferCreate(BUFFER_BYTES, 1);
    feed_lock = xSemaphoreCreateMutex();
    configASSERT(queue && feed_lock);
    ESP_LOGI(TAG, "I2S 16 kHz mono ready, volume %d, prebuffer %d ms", status.volume, CONFIG_CONTROL_PREBUFFER_MS);
    configASSERT(xTaskCreatePinnedToCore(playback_task, "audio_out", 4096, NULL, 7, NULL, 1) == pdPASS);
    configASSERT(xTaskCreatePinnedToCore(receiver_task, "audio_rx", 4096, NULL, 6, NULL, 0) == pdPASS);
}
