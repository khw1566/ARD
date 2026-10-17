#pragma once
#include "esp_err.h"

// Starts the task that owns all HTTP traffic to the PC server: status polling,
// heartbeat, queued commands/approvals and camera preview downloads.
esp_err_t hmi_net_start(void);
