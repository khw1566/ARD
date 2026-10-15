#pragma once

/* Waveshare ESP32-P4-Module-DEV-KIT, official schematic:
 * https://files.waveshare.com/wiki/ESP32-P4-Module-DEV-KIT/ESP32-P4-Module-DEV-KIT.pdf
 * Camera J4: pin 13 SCL, pin 14 SDA, pin 15 3.3V.
 * CSI_IO0 (pin 11) is pulled high; CSI_IO1 (pin 12) has no MCU clock connection.
 * Raspberry Pi OV5647 modules must supply their own sensor oscillator.
 */
#define CAM_SCCB_SCL_IO 8
#define CAM_SCCB_SDA_IO 7
#define BOARD_ETH_PHY_RESET_IO 51
