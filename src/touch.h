/*
 * CubeRadio - An ESP32-based internet radio player with MPD protocol support
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

#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>

// Define the maximum number of touch pins
#define TOUCH_PIN_COUNT 3

/**
 * @brief Touch button class for handling capacitive touch inputs
 * @details This class provides functionality for reading and debouncing
 * capacitive touch buttons on ESP32 touch pins. It supports both polling
 * and interrupt-based operation modes.
 * 
 * The class implements debouncing to prevent multiple detections from
 * a single physical touch due to electrical noise or unstable readings.
 * It can operate in either polling mode (regular state checks) or
 * interrupt mode (hardware interrupt triggered on touch detection).
 */
class TouchButton {
private:
  bool lastState;                       // Last stable state
  uint8_t pin;                          // Touch pin number
  uint32_t threshold;                   // Touch threshold value
  volatile unsigned long lastPressTime; // Last state change time for debouncing
  volatile bool pressedFlag;            // Flag indicating button press detected
  unsigned long debounceTime;           // Configurable debounce time
  bool useInterrupt;                    // Flag to indicate if interrupt mode is used
  bool valid = true;                    // False when the GPIO has no touch channel

public:
  /**
   * @brief Construct a new Touch Button object
   * @param touchPin The touch pin number
   * @param touchThreshold The touch threshold value (default 40)
   * @param debounceMs The debounce time in milliseconds (default 100)
   * @param useInterrupt Whether to use interrupt mode (default false)
   */
  TouchButton(uint8_t touchPin, uint32_t touchThreshold = 40, unsigned long debounceMs = 100, bool useInterrupt = false);

  /**
   * @brief Handle touch button state
   * This function should be called regularly in the main loop
   * 
   * This method implements debouncing logic to prevent multiple detections
   * from a single physical touch due to electrical noise or unstable readings.
   * It tracks the state changes and only registers a press after the state
   * has been stable for the configured debounce time.
   */
  void handle();

  /**
   * @brief Check if the button was pressed
   * @return true if button was pressed, false otherwise
   * 
   * This method implements a one-shot detection mechanism. Once a press is
   * detected, it returns true only once until the button is released and
   * pressed again. This prevents continuous detection while the button
   * is held down.
   */
  bool wasPressed();

  /**
   * @brief Get the current touch value
   * @return Current touch value
   *
   * This method reads the raw capacitance value from the touch pin.
   * Note: arduino-esp32 2.x values drop on touch; 3.x values rise.
   */
  uint32_t getTouchValue();

  /**
   * @brief Interrupt service routine for touch button
   * This function should be called from the ISR
   * 
   * This ISR is triggered when the touch pin value goes below the configured
   * threshold. It implements debouncing and sets the pressed flag for the
   * main code to process.
   */
  void handleInterrupt();
};

#endif // TOUCH_H
