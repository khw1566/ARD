#include "commands.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "CommandValidation.h"
#include "onboard_audio.h"
#include "pc_link.h"
#include "ard_network.h"

bool command_execute(const char *line, bool) {
    if (!line) return false;
    char cmd[128];
    while (*line && isspace((unsigned char)*line)) ++line;
    size_t n = strlcpy(cmd, line, sizeof(cmd));
    if (n >= sizeof(cmd)) return false;
    while (n && isspace((unsigned char)cmd[n - 1])) cmd[--n] = 0;
    for (char *p = cmd; *p; ++p) *p = (char)tolower((unsigned char)*p);
    if (!n) return false;
    if (onboard_audio_is_command(cmd)) return onboard_audio_command(cmd);
    if (!strcmp(cmd, "w")) { pc_drive_stop(); ard_network_print_setup(); return true; }
    if (!strcmp(cmd, "h") || !strcmp(cmd, "?")) { command_print_help(); return true; }
    if (!validDriveCommand(cmd)) return false;
    // All motor/sensor settings belong to the independent DriveMCU. The PC
    // applies the same manual/sorting interlocks as its own dashboard.
    if (!strcmp(cmd, "x") || !strcmp(cmd, "stop") || !strcmp(cmd, "d") || !strcmp(cmd, "disable"))
        pc_drive_stop();
    return pc_send_command(PC_DRIVE, cmd);
}

void command_print_help() {
    printf("\n--- JC1060 HMI ---\n"
           "Drive commands: forwarded through the PC to the existing DriveMCU.\n"
           "b<x>,<y> mxr<n> myr<n> mzr<n> home e d x stop\n"
           "Gamepad: START re-arm, B stop, sticks finite remote jog pulses.\n"
           "vol=<0..21> mute unmute audio_stop audio_status audio_test\n"
           "vad ptt audio_mic_on audio_mic_off; w Wi-Fi; h help\n"
           "server=<PC IP or host> (serial console only)\nJC1060> ");
    fflush(stdout);
}
