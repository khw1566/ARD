#pragma once
#include <stdbool.h>

// On-board WS2812B: red at boot, then a breathing colour per mode
// (N orange/dim, A blue = robot, B green = drive); red breathing while Wi-Fi is down.
void led_start(void);
// Voice button LED (GPIO4): on while the PC microphone is listening.
void led_mic(bool on);
