#pragma once
// ARD HMI screen: layout, drawing and touch handling. Portable C; the platform
// supplies a gfx_target_t and the hmi_port.h hooks.
#include <stdbool.h>
#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_W 240
#define HMI_H 320
// Largest camera preview the info panel can show.
#define HMI_PREVIEW_W 178
#define HMI_PREVIEW_H 160

void hmi_ui_init(const gfx_target_t *target);
// Boot screen with the logo (may be NULL) and a status line.
void hmi_ui_splash(const gfx_image_t *logo, const char *status);
// Leave the splash and show the main screen.
void hmi_ui_start(void);
// Call on each new press (not while held), in screen coordinates.
void hmi_ui_touch(int x, int y);
// Call every 10-50 ms; redraws only what changed.
void hmi_ui_tick(void);

#ifdef __cplusplus
}
#endif
