#pragma once
// Main tab: live view of the PC sorting sequence (five steps, disk turn, tray tilt, rail).
// Positions are the PC's confirmed step estimates; motion between them is animated.
// Both functions run in the LVGL task with the BSP lock held.
#include "lvgl.h"
#include "pc_link.h"

void ui_main_create(lv_obj_t *page);
void ui_main_poll(const PcStatus &pc, bool visible);
