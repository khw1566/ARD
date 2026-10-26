// LP owns the timer and result fields. HP owns the input sequence fields.
// No peripheral, PSRAM, FreeRTOS, logging, or automatic reset on the LP core.
#include <stdint.h>
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_interrupts.h"
#include "ulp_lp_core_lp_timer_shared.h"

volatile uint32_t ui_seq, audio_seq, audio_enabled;
volatile uint32_t monitor_ticks, stalled_mask;
static uint32_t last_ui, last_audio, ui_age, audio_age;

void LP_CORE_ISR_ATTR ulp_lp_core_lp_timer_intr_handler(void) {
    ulp_lp_core_lp_timer_intr_clear();
    const uint32_t ui = ui_seq, audio = audio_seq;
    ui_age = ui == last_ui ? ui_age + (ui_age < 10) : 0;
    audio_age = audio == last_audio ? audio_age + (audio_age < 10) : 0;
    last_ui = ui;
    last_audio = audio;
    stalled_mask = (ui_age >= 5 ? 1u : 0u) | (audio_enabled && audio_age >= 5 ? 2u : 0u);
    ++monitor_ticks;
    ulp_lp_core_lp_timer_set_wakeup_time(1000000);
}

int main(void) {
    ulp_lp_core_intr_enable();
    ulp_lp_core_lp_timer_set_wakeup_time(1000000);
    ulp_lp_core_lp_timer_intr_enable(true);
    for (;;) __asm__ volatile("wfi");
    return 0;
}
