#include <Arduino.h>
#include "esp_system.h"

// Leave brownout protection enabled and all peripherals idle.
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[POWER TEST] Setup reached. Reset reason: %d\n",
                static_cast<int>(esp_reset_reason()));
  Serial.printf("[POWER TEST] SDK: %s, CPU: %u MHz\n",
                ESP.getSdkVersion(), ESP.getCpuFreqMHz());
#ifdef BOARD_HAS_PSRAM
  Serial.printf("[POWER TEST] PSRAM found: %s, size: %u, free: %u bytes\n",
                psramFound() ? "yes" : "no", ESP.getPsramSize(),
                ESP.getFreePsram());
  Serial.println("[POWER TEST] PSRAM startup enabled; Camera/WiFi are idle.");
#else
  Serial.println("[POWER TEST] Camera/WiFi/PSRAM are not initialized.");
#endif
}

void loop() {
  Serial.printf("[POWER TEST] Alive: %lu ms, heap: %u bytes\n",
                millis(), ESP.getFreeHeap());
  delay(1000);
}
