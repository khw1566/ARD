#pragma once
// ESP32-C6 co-processor firmware check. An older ESP-Hosted C6 still answers control
// RPCs (scan, connect, association) but this board then never gets a DHCP lease and
// has no BT controller. The matching ESP-Hosted 2.12.2 slave image is embedded in the
// app and pushed over the existing SDIO link when the running C6 is older.
#include "esp_err.h"

// Call after the UI is up and before Wi-Fi starts. Restarts the board after a
// successful update; otherwise returns and Wi-Fi starts with the current C6.
esp_err_t c6_firmware_check();
