#include "buzzer.h"
#include <atomic>
#include "board.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace {

constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;

QueueHandle_t queue;
std::atomic<int> volume{64};       // commands/UI (0..255, duty of an 8-bit PWM)
constexpr int kHeartbeatVolume = 20; // quieter background blip

void tone(uint32_t hz, int duty) {
    ledc_set_freq(kMode, kTimer, hz);
    ledc_set_duty(kMode, kChannel, duty);
    ledc_update_duty(kMode, kChannel);
}

void quiet() {
    ledc_set_duty(kMode, kChannel, 0);
    ledc_update_duty(kMode, kChannel);
}

void wait(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void play(int type) {
    const int v = volume;
    switch (type) {
    case MELODY_HEARTBEAT:
        tone(1000, kHeartbeatVolume); wait(10);
        break;
    case MELODY_BOOT:
        tone(523, v); wait(100); tone(659, v); wait(100); tone(783, v); wait(100); tone(1046, v); wait(200);
        break;
    case MELODY_EMERGENCY:
        for (int i = 0; i < 4; ++i) { tone(1500, v); wait(80); quiet(); wait(40); }
        break;
    case MELODY_CONNECTION_LOST:
        for (int i = 0; i < 2; ++i) { tone(800, v); wait(200); tone(400, v); wait(200); }
        break;
    case MELODY_COMMAND:
        tone(2000, v); wait(50); quiet(); wait(30); tone(2500, v); wait(50);
        break;
    }
    quiet();
}

void buzzerTask(void *) {
    int type;
    for (;;) {
        if (xQueueReceive(queue, &type, portMAX_DELAY) != pdTRUE) continue;
        // As before, bv0 mutes UI sounds; the heartbeat blip has its own fixed volume.
        if (volume <= 0 && type != MELODY_HEARTBEAT) continue;
        play(type);
    }
}

} // namespace

esp_err_t buzzer_start() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = kMode;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.timer_num = kTimer;
    timer.freq_hz = 2000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;
    ledc_channel_config_t channel = {};
    channel.gpio_num = PIN_BUZZER;
    channel.speed_mode = kMode;
    channel.channel = kChannel;
    channel.timer_sel = kTimer;
    channel.duty = 0;
    if ((err = ledc_channel_config(&channel)) != ESP_OK) return err;
    queue = xQueueCreate(10, sizeof(int));
    if (!queue) return ESP_ERR_NO_MEM;
    return xTaskCreatePinnedToCore(buzzerTask, "buzzer", 2048, nullptr, 3, nullptr, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void buzzer_play(Melody melody) {
    const int type = melody;
    if (queue) xQueueSend(queue, &type, 0);
}

void buzzer_set_volume(int v) { volume = v < 0 ? 0 : v > 255 ? 255 : v; }
int buzzer_volume() { return volume; }
