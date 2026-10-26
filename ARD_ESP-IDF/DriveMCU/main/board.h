#pragma once
// DriveMCU (ESP32-WROOM-32D) pin map. Same wiring as the PlatformIO firmware.
// The manual-control inputs (SW1 GPIO13, SW2 GPIO14, POT GPIO34) are no longer used.
#include "driver/gpio.h"
#include "sdkconfig.h"

#ifdef CONFIG_ARD_ENABLE_RAIL_PID
#define ARD_RAIL_PID_ENABLED 1
#else
#define ARD_RAIL_PID_ENABLED 0
#endif

#ifdef CONFIG_ARD_ENABLE_TOF
#define ARD_TOF_ENABLED 1
#else
#define ARD_TOF_ENABLED 0
#endif

// Disc (X/Y differential pair) limits, menuconfig "ARD DriveMCU". Fallbacks for an
// sdkconfig generated before these options existed.
#ifdef CONFIG_ARD_DISK_MAX_SPEED
#define ARD_DISK_MAX_SPEED CONFIG_ARD_DISK_MAX_SPEED
#else
#define ARD_DISK_MAX_SPEED 300
#endif
#ifdef CONFIG_ARD_DISK_ACCEL
#define ARD_DISK_ACCEL CONFIG_ARD_DISK_ACCEL
#else
#define ARD_DISK_ACCEL 300
#endif
#ifdef CONFIG_ARD_DISK_TILT_LIMIT_DEG
#define ARD_DISK_TILT_LIMIT_DEG CONFIG_ARD_DISK_TILT_LIMIT_DEG
#else
#define ARD_DISK_TILT_LIMIT_DEG 60
#endif
#ifdef CONFIG_ARD_DISK_GEAR_RATIO
#define ARD_DISK_GEAR_RATIO CONFIG_ARD_DISK_GEAR_RATIO
#else
#define ARD_DISK_GEAR_RATIO 4
#endif

// A4988 drivers. EN is active LOW.
#define PIN_STEP_X GPIO_NUM_18
#define PIN_DIR_X  GPIO_NUM_19
#define PIN_EN_X   GPIO_NUM_21

#define PIN_STEP_Y GPIO_NUM_16
#define PIN_DIR_Y  GPIO_NUM_17
#define PIN_EN_Y   GPIO_NUM_5

#define PIN_STEP_Z GPIO_NUM_25   // rail
#define PIN_DIR_Z  GPIO_NUM_26
#define PIN_EN_Z   GPIO_NUM_27

// Shared I2C bus: AHT20 0x38, BMP280 0x76/0x77, VL53L0X 0x29, SSD1306 0x3C.
#define PIN_I2C_SDA GPIO_NUM_22
#define PIN_I2C_SCL GPIO_NUM_23

#define PIN_BUZZER GPIO_NUM_4    // passive buzzer (LEDC)

// NEO-6M GPS on UART2.
#define PIN_GPS_RX GPIO_NUM_32
#define PIN_GPS_TX GPIO_NUM_33
#define GPS_BAUD   9600

#define ARD_TCP_COMMAND_PORT 5002
#define ARD_PC_HTTP_PORT     5000
