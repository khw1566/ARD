#pragma once
// ESP32-2432S028 ("CYD 2.8") pin map. Same wiring as the PlatformIO build flags.

// ILI9341 on HSPI (IOMUX pins, so 40 MHz works without the GPIO matrix limit).
#define LCD_HOST        SPI2_HOST
#define LCD_PIN_SCLK    14
#define LCD_PIN_MOSI    13
#define LCD_PIN_MISO    12
#define LCD_PIN_CS      15
#define LCD_PIN_DC      2
#define LCD_PIN_BL      21
#define LCD_SPI_HZ      (40 * 1000 * 1000)

// XPT2046 resistive touch on its own pins. The Arduino build bit-banged these;
// here VSPI drives them through the GPIO matrix.
#define TOUCH_HOST      SPI3_HOST
#define TOUCH_PIN_SCLK  25
#define TOUCH_PIN_MOSI  32
#define TOUCH_PIN_MISO  39
#define TOUCH_PIN_CS    33
#define TOUCH_PIN_IRQ   36
#define TOUCH_SPI_HZ    (2 * 1000 * 1000)
