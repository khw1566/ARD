#pragma once
#include <stddef.h>

// TCP 5002 line commands from the PC (one connection per command is fine):
//   vol=N | volume=N   set volume 0..21 (saved)     -> "Volume set to N"
//   vol+ | vol-        step the volume
//   status             -> "vol=N muted=0 playing=0 mode=A sub=1"
//   stop               drop queued audio             -> "Audio stopped"
//   mute | play        mute / unmute the amplifier   -> "OK"
//   mic_ready | mic_done  voice button LED on / off -> "OK"
void cmd_server_start(void);
// Runs one command line; also used by the serial console. Writes a reply line.
void cmd_execute(const char *line, char *reply, size_t size);
