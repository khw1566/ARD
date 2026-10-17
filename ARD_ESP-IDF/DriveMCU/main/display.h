#pragma once
#include "ssd1306.h"

// Draws the status pages (network/GPS, motion/ToF, environment), rotating every 8 s.
// The manual-control menu screens were removed together with the buttons.
void display_render(Ssd1306 &oled);
