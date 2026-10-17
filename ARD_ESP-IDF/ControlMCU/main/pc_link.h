#pragma once

// Reports input state and heartbeats to the PC server (HTTP port 5000):
//   POST /api/control/update  on every input change, every 120 ms while the
//                             joystick is held (continuous jog), else every 2 s
//   POST /api/heartbeat       every 2 s
void pc_link_start(void);
// Input changed: send /api/control/update now.
void pc_link_notify_state(void);
// Ask the PC to stop all audio (voice queue, music, radio): POST /api/controlmcu/voice/stop.
void pc_link_request_stop(void);
// Board temperature in degrees C (internal sensor), NAN if unavailable.
float pc_link_temperature(void);
