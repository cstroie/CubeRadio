/*
 * CubeRadio - Pin definitions for ESP32-CAM
 * Copyright (C) 2025 Costin Stroie
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
PSRAM pins on ESP32-CAM:                                                                                               

 • IO6 - SPIQ (MISO)                                                                                                   
 • IO7 - SPID (MOSI)                                                                                                   
 • IO8 - SPIWP (Write Protect)                                                                                         
 • IO9 - SPIHD (Hold)                                                                                                  
 • IO16 - PSRAM Chip Select (CS)                                                                                       
 • IO17 - SPI Clock (CLK)                                                                                              

The ESP32-CAM typically uses:                                                                                          

 • IO0, IO4, IO15, IO16 for camera interface                                                                           
 • IO2, IO12, IO13 for SD card (if used)                                                                               
 • IO17 for flash/PSRAM clock                                                                                          

*/

#ifndef PINS_CAM_H
#define PINS_CAM_H

// Pin definitions for ESP32-CAM
// Note: Some pins are reserved for camera interface
#define DEFAULT_I2S_DOUT         15  ///< I2S Data Out pin
#define DEFAULT_I2S_BCLK         13  ///< I2S Bit Clock pin
#define DEFAULT_I2S_LRC          12  ///< I2S Left/Right Clock pin
#define DEFAULT_LED_PIN          33  ///< ESP32 internal LED pin
#define DEFAULT_ROTARY_CLK       -1  ///< Rotary encoder clock pin
#define DEFAULT_ROTARY_DT        -1  ///< Rotary encoder data pin
#define DEFAULT_ROTARY_SW        -1  ///< Rotary encoder switch pin
#define DEFAULT_TOUCH_PLAY        0  ///< Touch button play/pause pin
#define DEFAULT_TOUCH_NEXT        2  ///< Touch button next/volume-up pin
#define DEFAULT_TOUCH_PREV       -1  ///< Touch button previous/volume-down pin (GPIO 4 reassigned to display SCL)
#define DEFAULT_TOUCH_THRESHOLD  40  ///< Touch threshold value
#define DEFAULT_TOUCH_DEBOUNCE  100  ///< Touch debounce time in milliseconds
#define DEFAULT_BOARD_BUTTON     -1  ///< ESP32 board button pin (with internal pull-up resistor)
// Display I2C: GPIO 15 collided with I2S DOUT and GPIO 16 is the PSRAM chip
// select — both unusable. GPIO 14 and 4 are free when no SD card is fitted
// (4 also drives the flash LED; avoid 2: an I2C pull-up there blocks flashing).
#define DEFAULT_DISPLAY_SDA      14  ///< OLED display SDA pin
#define DEFAULT_DISPLAY_SCL       4  ///< OLED display SCL pin
#define DEFAULT_DISPLAY_TYPE      0  ///< OLED display type (index)
#define DEFAULT_DISPLAY_ADDR   0x3C  ///< OLED display I2C address
#define DEFAULT_DISPLAY_TIMEOUT  30  ///< Display timeout in seconds

#endif // PINS_CAM_H
