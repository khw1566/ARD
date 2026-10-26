#include "commands.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CommandValidation.h"
#include "buzzer.h"
#include "gps.h"
#include "motion.h"
#include "net/ard_network.h"
#include "sensors.h"
#include "telemetry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

SemaphoreHandle_t commandLock() { // one command at a time, from serial or TCP
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutex(); // thread-safe static init
    return mutex;
}

bool starts(const char *text, const char *prefix) { return !strncmp(text, prefix, strlen(prefix)); }
long integer(const char *text) { return strtol(text, nullptr, 10); }

// Shared handler for mx*/my*/mz* and the legacy X-only r/s/a commands.
bool axisCommand(Axis axis, const char *sub) {
    const char name = "XYZ"[axis];
    if (sub[0] == 'r') {
        motion_clear_stop();
        const int32_t steps = (int32_t)integer(sub + 1);
        return motion_queue(axis == AXIS_X ? steps : 0, axis == AXIS_Y ? steps : 0, axis == AXIS_Z ? steps : 0, "xyz"[axis]);
    }
    if (sub[0] == 's') {
        const int value = (int)integer(sub + 1);
        if (!motion_set_speed(axis, value)) return false;
        log_async("Motor %c speed: %d", name, value);
        return true;
    }
    if (sub[0] == 'a') {
        const int value = (int)integer(sub + 1);
        if (!motion_set_accel(axis, value)) return false;
        log_async("Motor %c accel: %d", name, value);
        return true;
    }
    if (!strcmp(sub, "x")) {
        if (!motion_stop_axis(axis)) return false;
        log_async("Motor %c STOP", name);
        return true;
    }
    if (!strcmp(sub, "e") || !strcmp(sub, "d")) {
        motion_enable(axis, sub[0] == 'e');
        log_async("Motor %c %s", name, sub[0] == 'e' ? "Enabled" : "Disabled");
        return true;
    }
    return false;
}

bool disk(const char *cmd, int sx, int sy) {
    motion_clear_stop();
    long steps = integer(cmd + 2);
    if (!steps) steps = 200;
    return motion_queue((int32_t)(sx * steps), (int32_t)(sy * steps), 0, 'b');
}

bool execute(const char *cmd) {
    if (starts(cmd, "mx")) { motion_clear_stop(); return axisCommand(AXIS_X, cmd + 2); }
    if (starts(cmd, "my")) { motion_clear_stop(); return axisCommand(AXIS_Y, cmd + 2); }
    if (starts(cmd, "df")) return disk(cmd, 1, -1);  // disk forward
    if (starts(cmd, "db")) return disk(cmd, -1, 1);  // disk backward
    if (starts(cmd, "dr")) return disk(cmd, 1, 1);   // rotate CW
    if (starts(cmd, "dl")) return disk(cmd, -1, -1); // rotate CCW
    if (starts(cmd, "mzp")) { const float v = strtof(cmd + 3, nullptr); if (!motion_set_pid_p(v)) return false; log_async("PID P set: %.2f", v); return true; }
    if (starts(cmd, "mzi")) { const float v = strtof(cmd + 3, nullptr); if (!motion_set_pid_i(v)) return false; log_async("PID I set: %.2f", v); return true; }
    if (starts(cmd, "mzd")) { const float v = strtof(cmd + 3, nullptr); if (!motion_set_pid_d(v)) return false; log_async("PID D set: %.2f", v); return true; }
    if (starts(cmd, "gps")) {
        const bool on = integer(cmd + 3) == 1;
        gps_set_enabled(on);
        if (on) telemetry_request_gps_sync();
        log_async("GPS Module: %s", on ? "ON" : "OFF");
        return true;
    }
    if (starts(cmd, "mzt")) {
        const int mm = (int)integer(cmd + 3);
        if (!motion_set_rail_target(mm)) return false; // outside the Z safety limits (logged)
        log_async("Target ToF: %d", mm);
        return true;
    }
    if (starts(cmd, "mzl")) { // mzl<min>,<max>: Z safety limits on (ToF mm); mzl0: off
        if (!strcmp(cmd + 3, "0")) {
            motion_set_rail_limits(false, 0, 0);
            log_async("Z safety limits OFF (bench test only)");
            return true;
        }
        const char *comma = strchr(cmd + 3, ',');
        const int minMm = (int)integer(cmd + 3), maxMm = comma ? (int)integer(comma + 1) : 0;
        if (!comma || !motion_set_rail_limits(true, minMm, maxMm)) {
            log_async("Z safety limits rejected: mzl<min>,<max> with 1..2000 mm and max-min >= 10");
            return false;
        }
        log_async("Z safety limits: %d..%d mm (ToF)", minMm, maxMm);
        return true;
    }
    if (starts(cmd, "mzv")) {
        const int dir = (int)integer(cmd + 3);
        if (!motion_set_rail_dir(dir)) { log_async("Rail direction rejected: turn the rail PID off first"); return false; }
        log_async("Rail direction: %+d (+Z steps %s the ToF distance)", dir, dir > 0 ? "lengthen" : "shorten");
        return true;
    }
    if (starts(cmd, "mzc")) {
        const long hz = integer(cmd + 3);
        sensors_request_i2c_clock((uint32_t)hz); // applied by the I2C owner task
        log_async("I2C Clock set: %ld", hz);
        return true;
    }
    if (starts(cmd, "mzh")) {
        const long ms = integer(cmd + 3);
        telemetry_set_heartbeat_interval((uint32_t)ms);
        log_async("Heartbeat Interval set: %ld", ms);
        return true;
    }
    if (starts(cmd, "mzm")) {
        const bool on = integer(cmd + 3) == 1;
        if (!motion_set_pid_enabled(on)) { log_async("Rail PID rejected: PID blocked in this build or ToF unavailable"); return false; }
        log_async("Rail PID Mode: %s", on ? "ON" : "OFF");
        return true;
    }
    if (starts(cmd, "mzs")) {
        // The HMI's open-loop "STP +50 / -50" buttons send mzs+50 / mzs-50 as a UI step size.
        // The old firmware accepted mzs+50 as Steps/mm = 50 and lost the rail calibration,
        // so only an unsigned value is a calibration write now.
        if (cmd[3] == '+' || cmd[3] == '-') {
            log_async("mzs%s ignored: signed value is an HMI step size, not Steps/mm", cmd + 3);
            return false;
        }
        const float factor = strtof(cmd + 3, nullptr);
        if (!motion_set_steps_per_mm(factor)) return false;
        log_async("Steps/mm Z set to: %.2f", factor);
        return true;
    }
    if (!strcmp(cmd, "mza")) {
        log_async("Auto calibration unavailable; set measured Steps/mm with mzs<value>.");
        return false;
    }
    if (!strcmp(cmd, "home")) {
        motion_home(); // Z remains at its current position, rail PID off
        log_async("System Homing Initiated from PC.");
        return true;
    }
    if (!strcmp(cmd, "zero")) {
        if (!motion_set_origin()) { log_async("Set origin rejected: wait until all axes stop"); return false; }
        log_async("Current position set as origin (X/Y/Z = 0)");
        return true;
    }
    if (starts(cmd, "mz")) { motion_clear_stop(); return axisCommand(AXIS_Z, cmd + 2); }
    if (cmd[0] == 'j') { // j<x>,<y>,<z>: jog velocity in 1/1000 of each axis max speed
        const char *y = strchr(cmd, ',') + 1, *z = strchr(y, ',') + 1;
        return motion_jog(integer(cmd + 1) / 1000.0f, integer(y) / 1000.0f, integer(z) / 1000.0f);
    }
    if (cmd[0] == 'b' && cmd[1] != 'v' && strchr(cmd, ',')) { // b<stepsX>,<stepsY>
        motion_clear_stop();
        const char *comma = strchr(cmd, ',');
        return motion_queue((int32_t)integer(cmd + 1), (int32_t)integer(comma + 1), 0, 'b');
    }
    if (!strcmp(cmd, "x") || !strcmp(cmd, "stop")) {
        motion_emergency_stop();
        log_async("ALL Motors EMERGENCY STOP & Queue Cleared");
        buzzer_play(MELODY_EMERGENCY);
        return true;
    }
    if (starts(cmd, "md")) {
        const long ms = integer(cmd + 2);
        motion_set_inter_command_delay((uint32_t)ms);
        log_async("Inter-command Delay: %ldms", ms);
        return true;
    }
    if (starts(cmd, "bv")) {
        buzzer_set_volume((int)integer(cmd + 2));
        log_async("Buzzer Volume: %d", buzzer_volume());
        return true;
    }
    if (!strcmp(cmd, "e") || !strcmp(cmd, "enable")) { motion_enable_all(true); log_async("ALL Motors Enabled"); return true; }
    if (!strcmp(cmd, "d") || !strcmp(cmd, "disable")) { motion_emergency_stop(); log_async("ALL Motors Disabled"); return true; }
    if (!strcmp(cmd, "w")) { motion_emergency_stop(); ard_network_print_setup(); return true; }
    if (!strcmp(cmd, "h") || !strcmp(cmd, "?")) { command_print_help(); return true; }
    // Legacy short commands act on X.
    if (cmd[0] == 'r' || cmd[0] == 's' || cmd[0] == 'a') {
        if (cmd[0] == 'r') motion_clear_stop();
        return axisCommand(AXIS_X, cmd);
    }
    return false;
}

} // namespace

bool command_execute(const char *line) {
    char cmd[128];
    // trim + lowercase, as the Arduino String version did
    while (*line && isspace((unsigned char)*line)) ++line;
    size_t n = strlcpy(cmd, line, sizeof(cmd));
    if (n >= sizeof(cmd)) return false;
    while (n && isspace((unsigned char)cmd[n - 1])) cmd[--n] = 0;
    for (char *p = cmd; *p; ++p) *p = (char)tolower((unsigned char)*p);
    if (!n || !validDriveCommand(cmd)) return false;

    // STOP must not queue behind another command.
    const bool urgent = !strcmp(cmd, "x") || !strcmp(cmd, "stop");
    const bool jog = cmd[0] == 'j'; // streamed about 10 times a second: no beep or echo
    if (!urgent && xSemaphoreTake(commandLock(), pdMS_TO_TICKS(500)) != pdTRUE) return false;
    if (!urgent && !jog) buzzer_play(MELODY_COMMAND);
    if (!jog) printf("\nExecuting: %s\n", cmd);
    const bool accepted = execute(cmd);
    if (!urgent) xSemaphoreGive(commandLock());
    return accepted;
}

void command_print_help() {
    printf("\n--- DriveMCU Commands ---\n"
           "  mxr<steps> / myr<steps> / mzr<steps>  - relative move X / Y / Z\n"
           "  mxs<v> mys<v>  - disc max speed (steps/s, capped at 300 by default)\n"
           "  disc tilt (X-Y)/2 is held within +-60 deg; disc rotation is free\n"
           "  mxa<v> mya<v> mza<v>  - acceleration\n"
           "  mxx myx mzx  - decelerating stop of one axis\n"
           "  mxe mxd mye myd  - enable / disable one driver\n"
           "  b<x>,<y>  - move X and Y together\n"
           "  j<x>,<y>,<z>  - jog velocity, -1000..1000 of max speed (resend < 400 ms)\n"
           "  df<n> db<n> dr<n> dl<n>  - disk forward / back / rotate\n"
           "  home  - X/Y to 0\n"
           "  zero  - set the current X/Y/Z position as 0 (only at rest)\n"
           "  mzm<0|1> mzt<mm> mzp/mzi/mzd<v> mzs<steps/mm>  - rail PID\n"
           "  mzv<1|-1>  - rail direction: +Z steps lengthen (1) / shorten (-1) the ToF distance\n"
           "  mzl<min>,<max>  - Z safety limits in ToF mm (default 70,120); mzl0 - off (bench test)\n"
           "  md<ms>  - inter-command delay   mzh<ms> - heartbeat interval\n"
           "  mzc<hz> - I2C clock   gps<0|1>   bv<0..255> - buzzer volume\n"
           "  e / d  - enable / disable all   x or stop - EMERGENCY STOP\n"
           "  w  - Wi-Fi setup info   h or ?  - this help\n"
           "\nDriveMCU> ");
    fflush(stdout);
}
