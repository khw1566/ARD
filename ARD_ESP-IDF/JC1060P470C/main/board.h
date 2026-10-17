#pragma once
// GUITION JC1060P470C (ESP32-P4 + ESP32-C6-MINI-1U, 7" 1024x600 JD9165 MIPI-DSI, GT911).
// One board replaces the old HMI (CYD ESP32 + ILI9341) and DriveMCU (ESP32-WROOM32D).
//
// Fixed on-board wiring (sources in README):
//   I2C SDA 7 / SCL 8      GT911 (0x5D or 0x14), ES8311, ES7210, RTC; our sensors join this bus.
//   C6 SDIO CLK 18, CMD 19, D0..D3 14..17, C6 reset 54 (same as ImageSenderMCU_P4).
//   Backlight GPIO23, LCD reset GPIO0 (Kconfig, unverified), speaker amp enable GPIO11 (unused).
// Expansion header (2x13): GPIO1..5, 20, 32, 33, 45..47. Motors, buzzer and GPS default there.
#include "driver/gpio.h"
#include "sdkconfig.h"

#ifdef CONFIG_ARD_ENABLE_TOF
#define ARD_TOF_ENABLED 1
#else
#define ARD_TOF_ENABLED 0
#endif

#define LCD_H_RES 1024
#define LCD_V_RES 600

#define PIN_I2C_SDA GPIO_NUM_7
#define PIN_I2C_SCL GPIO_NUM_8
#define PIN_SPK_AMP_EN GPIO_NUM_11

#define PIN_LCD_RST   ((gpio_num_t)CONFIG_ARD_PIN_LCD_RST)
#define PIN_BACKLIGHT ((gpio_num_t)CONFIG_ARD_PIN_BACKLIGHT)

// A4988 drivers. EN is active LOW.
#define PIN_STEP_X ((gpio_num_t)CONFIG_ARD_PIN_STEP_X)
#define PIN_DIR_X  ((gpio_num_t)CONFIG_ARD_PIN_DIR_X)
#define PIN_STEP_Y ((gpio_num_t)CONFIG_ARD_PIN_STEP_Y)
#define PIN_DIR_Y  ((gpio_num_t)CONFIG_ARD_PIN_DIR_Y)
#define PIN_STEP_Z ((gpio_num_t)CONFIG_ARD_PIN_STEP_Z)
#define PIN_DIR_Z  ((gpio_num_t)CONFIG_ARD_PIN_DIR_Z)
#define PIN_EN_X   ((gpio_num_t)CONFIG_ARD_PIN_EN_X)
#define PIN_EN_Y   ((gpio_num_t)CONFIG_ARD_PIN_EN_Y)
#define PIN_EN_Z   ((gpio_num_t)CONFIG_ARD_PIN_EN_Z)

#define PIN_BUZZER ((gpio_num_t)CONFIG_ARD_PIN_BUZZER)
#define PIN_GPS_RX ((gpio_num_t)CONFIG_ARD_PIN_GPS_RX)
#define PIN_GPS_TX ((gpio_num_t)CONFIG_ARD_PIN_GPS_TX)
#define GPS_BAUD   9600

#define ARD_TCP_COMMAND_PORT 5002
#define ARD_PC_HTTP_PORT     5000
