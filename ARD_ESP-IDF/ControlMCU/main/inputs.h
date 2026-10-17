#pragma once
#include <stdbool.h>

typedef enum { JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_COUNT } joy_dir_t;

typedef struct {
    char mode;      // 'A', 'N' (centre) or 'B' from the 3-position switch
    int sub_mode;   // 2 while the latching voice button is down, else 1
    bool joy[JOY_COUNT];
} control_state_t;

typedef void (*inputs_change_cb_t)(const control_state_t *state);

// Starts the 5 ms debounced input scan; on_change runs in the scan task.
void inputs_start(inputs_change_cb_t on_change);
void inputs_get(control_state_t *out);
bool inputs_any_joy(const control_state_t *state);
