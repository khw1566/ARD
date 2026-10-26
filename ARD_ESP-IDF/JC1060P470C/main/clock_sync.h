#pragma once
#include "esp_err.h"

esp_err_t clock_sync_start();
// UTC Unix seconds from the local PC; ignored when absent or implausible.
void clock_sync_from_pc(double unix_seconds);
