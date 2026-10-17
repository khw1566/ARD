#pragma once
// ESP32-S3 N16R8 IPEX ControlMCU board, same wiring as ARD_software/ControlMCU.

// Inputs (active low, internal pull-ups)
#define PIN_JOY_UP     47
#define PIN_JOY_DOWN   21
#define PIN_JOY_LEFT   13
#define PIN_JOY_RIGHT  14
#define PIN_TOGGLE_A   35  // 3-position switch: A side
#define PIN_TOGGLE_B   36  // 3-position switch: B side (both high = N)
#define PIN_VOICE_BTN  11  // latching push switch = voice input

// Outputs
#define PIN_BUTTON_LED 4   // voice button LED (on while the PC mic listens)
#define PIN_RGB_LED    48  // on-board WS2812B

// MAX98357A I2S amplifier
#define PIN_I2S_DOUT   40
#define PIN_I2S_WS     41
#define PIN_I2S_BCLK   42
#define PIN_AMP_SD     39  // high = on, low = shutdown

// Network
#define PC_HTTP_PORT      5000
#define AUDIO_UDP_PORT    5005  // PC -> board 16 kHz mono s16le PCM, and "vol=N"
#define COMMAND_TCP_PORT  5002  // PC -> board line commands
#define AUDIO_SAMPLE_RATE 16000
