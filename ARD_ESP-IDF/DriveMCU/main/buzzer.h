#pragma once
#include "esp_err.h"

enum Melody {
    MELODY_HEARTBEAT = 1,
    MELODY_BOOT = 2,
    MELODY_EMERGENCY = 3,
    MELODY_CONNECTION_LOST = 4,
    MELODY_COMMAND = 5,
    // 6..8 were the manual-menu sounds and are removed with the manual controls.
};

esp_err_t buzzer_start();
void buzzer_play(Melody melody);  // non-blocking; dropped when the queue is full
void buzzer_set_volume(int volume); // 0..255
int buzzer_volume();
