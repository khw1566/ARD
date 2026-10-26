#include "runtime_health.h"
#include "task_policy.h"
#include "onboard_audio.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#if CONFIG_ULP_COPROC_ENABLED && CONFIG_ULP_COPROC_TYPE_LP_CORE
#include "ulp_lp_core.h"
#include "ulp_ard_health.h"
extern const uint8_t lpStart[] asm("_binary_ulp_ard_health_bin_start");
extern const uint8_t lpEnd[] asm("_binary_ulp_ard_health_bin_end");
#endif

namespace {
std::atomic<uint32_t> uiLast{0}, audioLast{0}, maxUiGap{0}, maxAudioGap{0};
std::atomic<bool> audioEnabled{false}, lpReady{false}, printRequested{false};
uint32_t ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
void maximum(std::atomic<uint32_t> &target, uint32_t value) {
    uint32_t old = target.load();
    while (old < value && !target.compare_exchange_weak(old, value)) {}
}

void report() {
    // Bounded storage, no heap allocation and no repeated task-handle lookup.
    static TaskStatus_t states[80];
    static uint32_t previousIdle[2], previousTotal;
    static bool sampled;
    configRUN_TIME_COUNTER_TYPE total = 0;
    const UBaseType_t count = uxTaskGetSystemState(states, 80, &total);
    if (!count) {
        ESP_LOGW("scheduler", "Task snapshot unavailable (capacity 80); CPU load unknown");
        return;
    }
    uint32_t idle[2]{};
    bool found[2]{};
    const uint32_t dt = static_cast<uint32_t>(total) - previousTotal;
    for (UBaseType_t i = 0; i < count; ++i) {
        const TaskStatus_t &s = states[i];
        if (!strcmp(s.pcTaskName, "IDLE0")) { idle[0] = s.ulRunTimeCounter; found[0] = true; }
        if (!strcmp(s.pcTaskName, "IDLE1")) { idle[1] = s.ulRunTimeCounter; found[1] = true; }
        const bool application = !strcmp(s.pcTaskName, "lvgl") || !strcmp(s.pcTaskName, "voice_io") ||
            !strcmp(s.pcTaskName, "voice_rx") || !strcmp(s.pcTaskName, "voice_upload") ||
            !strcmp(s.pcTaskName, "visitor_voice") || !strcmp(s.pcTaskName, "pc_link") ||
            !strcmp(s.pcTaskName, "drive_control") || !strcmp(s.pcTaskName, "ui_settings") ||
            !strcmp(s.pcTaskName, "tcp_cmd") || !strcmp(s.pcTaskName, "console") || !strcmp(s.pcTaskName, "health");
        if (s.usStackHighWaterMark < (application ? 768 : 256))
            ESP_LOGW("scheduler", "Low stack: %s free=%u bytes priority=%u", s.pcTaskName,
                     static_cast<unsigned>(s.usStackHighWaterMark), static_cast<unsigned>(s.uxCurrentPriority));
    }
    // Unsigned interval deltas handle the 32-bit ESP timer stats wrap (~71 min).
    // First sample is a baseline; no CPU percentage is fabricated without stats.
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    if (sampled && dt && found[0] && found[1]) {
        unsigned busy[2];
        for (unsigned core = 0; core < 2; ++core) {
            uint64_t freePct = static_cast<uint64_t>(idle[core] - previousIdle[core]) * 100 / dt;
            busy[core] = freePct > 100 ? 0 : 100 - freePct;
        }
        ESP_LOGI("scheduler", "HP0 busy=%u%% HP1 busy=%u%% interval=%lu us tasks=%u",
                 busy[0], busy[1], static_cast<unsigned long>(dt), static_cast<unsigned>(count));
    } else ESP_LOGI("scheduler", "CPU stats baseline; tasks=%u", static_cast<unsigned>(count));
#else
    ESP_LOGW("scheduler", "Runtime counters disabled; CPU load unknown");
#endif
    previousIdle[0] = idle[0]; previousIdle[1] = idle[1]; previousTotal = total; sampled = true;
    const uint32_t now = ms();
    ESP_LOGI("scheduler", "UI age=%lu max_gap=%lu ms; audio enabled=%d age=%lu max_gap=%lu ms; internal=%u DMA=%u PSRAM=%u",
             static_cast<unsigned long>(now - uiLast.load()), static_cast<unsigned long>(maxUiGap.load()),
             audioEnabled.load(), static_cast<unsigned long>(now - audioLast.load()),
             static_cast<unsigned long>(maxAudioGap.load()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
#if CONFIG_ULP_COPROC_ENABLED && CONFIG_ULP_COPROC_TYPE_LP_CORE
    static uint32_t previousLp;
    const uint32_t ticks = static_cast<volatile uint32_t &>(ulp_monitor_ticks);
    const uint32_t mask = static_cast<volatile uint32_t &>(ulp_stalled_mask);
    ESP_LOGI("scheduler", "LP ready=%d ticks=%lu stalled_mask=%lu (UI=1 audio=2)", lpReady.load(),
             static_cast<unsigned long>(ticks), static_cast<unsigned long>(mask));
    if (lpReady.load() && (ticks == previousLp || mask))
        ESP_LOGW("scheduler", "LP/HP progress needs inspection; no automatic reset issued");
    previousLp = ticks;
#endif
}

void monitor(void *) {
    uint32_t lastReport = ms();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (printRequested.exchange(false) || ms() - lastReport >= 30000) {
            report();
            lastReport = ms();
        }
    }
}
} // namespace

void runtime_health_beat(HealthBeat beat) {
    auto &last = beat == HealthBeat::Ui ? uiLast : audioLast;
    const uint32_t now = ms(), prior = last.exchange(now);
    if (prior) maximum(beat == HealthBeat::Ui ? maxUiGap : maxAudioGap, now - prior);
#if CONFIG_ULP_COPROC_ENABLED && CONFIG_ULP_COPROC_TYPE_LP_CORE
    if (lpReady.load()) {
        volatile uint32_t &sequence = beat == HealthBeat::Ui ?
            static_cast<volatile uint32_t &>(ulp_ui_seq) : static_cast<volatile uint32_t &>(ulp_audio_seq);
        sequence = sequence + 1;
    }
#endif
}
void runtime_health_audio_enabled(bool enabled) {
    audioEnabled.store(enabled);
#if CONFIG_ULP_COPROC_ENABLED && CONFIG_ULP_COPROC_TYPE_LP_CORE
    if (lpReady.load()) static_cast<volatile uint32_t &>(ulp_audio_enabled) = enabled;
#endif
}
void runtime_health_print() { printRequested.store(true); }

esp_err_t runtime_health_start() {
#if CONFIG_ULP_COPROC_ENABLED && CONFIG_ULP_COPROC_TYPE_LP_CORE
    esp_err_t err = ulp_lp_core_load_binary(lpStart, lpEnd - lpStart);
    if (err == ESP_OK) {
        static_cast<volatile uint32_t &>(ulp_audio_enabled) = audioEnabled.load();
        ulp_lp_core_cfg_t cfg{};
        cfg.wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_HP_CPU;
        err = ulp_lp_core_run(&cfg);
    }
    lpReady.store(err == ESP_OK);
    if (err != ESP_OK) ESP_LOGW("scheduler", "LP monitor unavailable: %s", esp_err_to_name(err));
#endif
    return xTaskCreatePinnedToCore(monitor, "health", 4096, nullptr, 1, nullptr, ARD_CORE_SERVICE) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}
