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

#ifndef ROTARY_H
#define ROTARY_H

#include <Arduino.h>

// Forward declarations
class RotaryEncoder;

// Function declarations
void setupRotaryEncoder();
void rotaryISR();
void rotarySwISR();

/**
 * @brief Rotary Encoder Handler Class
 * @details This class manages the rotary encoder hardware for volume control
 * and playlist navigation. It handles both rotation detection and button
 * press events with proper debouncing to ensure reliable operation.
 * 
 * The rotary encoder uses a quadrature encoding scheme where two signals
 * (CLK and DT) change state in a specific sequence depending on the rotation
 *  direction.
 */
class RotaryEncoder {
private:
  volatile int position = 0;                    ///< Current rotary encoder position counter
  volatile unsigned long lastRotaryTime = 0;    ///< Last rotary event timestamp for debouncing
  volatile unsigned long lastInterruptTime = 0; ///< Last button interrupt timestamp for debouncing
  volatile bool buttonPressedFlag = false;      ///< Flag indicating button press detected

public:
  /**
   * @brief Handle rotary encoder rotation
   * @details Processes rotation events by detecting CLK signal edges and 
   * determining rotation direction based on the DT signal state. Implements 
   * 100ms debouncing to prevent false readings from electrical noise.
   * 
   * The quadrature encoding works as follows:
   * - When rotating clockwise: CLK leads DT
   * - When rotating counter-clockwise: DT leads CLK
   * 
   * Only processes events when CLK transitions from HIGH to LOW to avoid 
   * double-counting.
   */
  void handleRotation();
  
  /**
   * @brief Handle button press
   * @details Processes button press events with 100ms debouncing to prevent
   * multiple detections from a single press. Sets an internal flag that can
   * be checked and cleared by wasButtonPressed().
   * 
   * The button is connected with a pull-up resistor, so a press is detected
   * when the signal transitions from HIGH to LOW (falling edge). However, this
   * function is triggered by an interrupt on the falling edge, so we only need
   * to implement debouncing based on time since last interrupt.
   */
  void handleButtonPress();
  
  /**
   * @brief Get current position
   * @details Returns the current rotary encoder position counter value.
   * The position increases with clockwise rotation and decreases with
   * counter-clockwise rotation.
   * @return Current position value
   */
  int getPosition() const volatile;
  
  /**
   * @brief Set position
   * @details Sets the rotary encoder position counter to a specific value.
   * This can be used to reset the position or synchronize with external state.
   * @param pos New position value
   */
  void setPosition(int pos);
  
  /**
   * @brief Check if button was pressed
   * @details Returns the button press status and automatically clears the flag.
   * This ensures that each button press is only processed once.
   * @return true if button was pressed since last check, false otherwise
   */
  bool wasButtonPressed();
};

#endif
