#pragma once
// What the UI needs from the platform. The firmware implements these in
// hmi_port_esp.c; the PC simulator provides its own versions.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gfx.h"
#include "hmi_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { TARGET_DRIVE, TARGET_ROBOT, TARGET_CAM, TARGET_SOUND } hmi_target_t;

uint32_t hmi_millis(void);
// Local wall-clock "HH:MM"; returns false until time is synced.
bool hmi_clock(char *out, size_t size);

// Copies the latest model under the platform's lock.
void hmi_model_get(hmi_model_t *out);
// Applies an optimistic UI change; the platform keeps it over server values for a
// short hold period so a poll that was already in flight cannot undo it.
void hmi_model_edit(void (*edit)(hmi_model_t *m, void *arg), void *arg);

// Fire-and-forget; the platform sends from its own task so the UI never blocks.
void hmi_send_command(hmi_target_t target, const char *command);
void hmi_send_approval(const char *id, bool approve);
void hmi_request_refresh(void);

// Camera preview: the platform fetches /api/hmi/last_image while wanted.
void hmi_image_want(bool wanted);
// Returns the newest decoded preview (or NULL) and its sequence number. The
// image stays valid until hmi_image_release().
const gfx_image_t *hmi_image_acquire(uint32_t *seq, const char **error);
void hmi_image_release(void);

#ifdef __cplusplus
}
#endif
