#pragma once
// Text command protocol shared by the serial console and TCP port 5002.
// Same commands as the PlatformIO firmware; validation is CommandValidation.h.
#include "esp_err.h"

// Runs one command line; returns false when it is rejected (sent as ERR:REJECTED).
bool command_execute(const char *line, bool announce = true);
void command_print_help();

// Serial console (UART0) and TCP command server tasks.
esp_err_t command_servers_start();
