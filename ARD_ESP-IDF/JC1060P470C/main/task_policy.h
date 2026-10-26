#pragma once
// ESP32-P4 HP0: networking/storage; HP1: display and deterministic audio I/O.
// LP runs its own small monitor program, outside the HP FreeRTOS scheduler.
constexpr int ARD_CORE_SERVICE = 0;
constexpr int ARD_CORE_REALTIME = 1;
