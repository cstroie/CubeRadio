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

#include "rotary.h"
#include "main.h"

// Forward declaration of the global rotary encoder instance
extern RotaryEncoder rotaryEncoder;

/**
 * @brief Interrupt service routine for rotary encoder
 * Handles rotary encoder rotation events
 */
void rotaryISR() {
  rotaryEncoder.handleRotation();
}

/**
 * @brief Interrupt service routine for rotary switch button
 * Handles rotary switch button press events
 */
void rotarySwISR() {
  rotaryEncoder.handleButtonPress();
}

/**
 * @brief Initialize rotary encoder hardware
 * Configures pins and attaches interrupt handlers for the rotary encoder
 * This function sets up the rotary encoder pins with internal pull-up resistors
 * and attaches interrupt handlers for rotation and button press events.
 */
void setupRotaryEncoder() {
  // Configure rotary encoder pins with internal pull-up resistors
  pinMode(config.rotary_clk, INPUT_PULLUP);   // Enable internal pull-up resistor
  pinMode(config.rotary_dt,  INPUT_PULLUP);   // Enable internal pull-up resistor
  pinMode(config.rotary_sw,  INPUT_PULLUP);   // Enable internal pull-up resistor
  // Attach interrupt handler for rotary encoder rotation
  attachInterrupt(digitalPinToInterrupt(config.rotary_clk), rotaryISR, FALLING);
  // Attach interrupt handler for rotary encoder button press
  attachInterrupt(digitalPinToInterrupt(config.rotary_sw), rotarySwISR, FALLING);
}

/**
 * @brief Handle rotary encoder rotation
 * @details Processes rotation events by detecting CLK signal edges and 
 * determining rotation direction based on the DT signal state. Implements 100ms 
 * debouncing to prevent false readings from electrical noise.
 * 
 * The quadrature encoding works as follows:
 * - When rotating clockwise: CLK leads DT
 * - When rotating counter-clockwise: DT leads CLK
 * 
 * Only processes events when CLK transitions from HIGH to LOW to avoid
 * double-counting.
 */
void RotaryEncoder::handleRotation() {
  unsigned long currentTime = millis();
  // Debounce rotary encoder (ignore if less than 100ms since last event)
  if (currentTime - lastRotaryTime < 100) {
    return;
  }
  // Read data signal
  int DT = digitalRead(config.rotary_dt);
  // CLK is already LOW due to FALLING interrupt trigger
  // Determine rotation direction based on DT state at the time of CLK falling edge
  // In quadrature encoding, when CLK falls:
  // - If DT is HIGH, rotation is clockwise
  // - If DT is LOW, rotation is counter-clockwise
  if (DT == HIGH) {
    position++;      // Clockwise rotation increments position
  } else {
    position--;      // Counter-clockwise rotation decrements position
  }
  lastRotaryTime = currentTime;  // Update last event time for debouncing
}

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
void RotaryEncoder::handleButtonPress() {
  unsigned long interruptTime = millis();
  // Debounce the button press (ignore if less than 100ms since last press)
  // This prevents multiple detections from a single physical button press
  // due to mechanical switch bouncing
  if (interruptTime - lastInterruptTime > 100) {
    // Set flag to indicate button press detected
    buttonPressedFlag = true;
  }
  // Update last interrupt time for debouncing
  lastInterruptTime = interruptTime;
}

/**
 * @brief Get current position
 * @details Returns the current rotary encoder position counter value.
 * The position increases with clockwise rotation and decreases with
 * counter-clockwise rotation.
 * @return Current position value
 */
int RotaryEncoder::getPosition() const volatile {
  // Disable interrupts temporarily to ensure atomic read of multi-byte position
  noInterrupts();
  int pos = position;
  interrupts();
  return pos;
}

/**
 * @brief Set position
 * @details Sets the rotary encoder position counter to a specific value.
 * This can be used to reset the position or synchronize with external state.
 * @param pos New position value
 */
void RotaryEncoder::setPosition(int pos) {
  position = pos;
}

/**
 * @brief Check if button was pressed
 * @details Returns the button press status and automatically clears the flag.
 * This ensures that each button press is only processed once.
 * @return true if button was pressed since last check, false otherwise
 */
bool RotaryEncoder::wasButtonPressed() {
  // Read and clear the flag atomically: reading it before the critical
  // section would lose a press if the ISR fired between read and clear
  noInterrupts();
  bool result = buttonPressedFlag;
  buttonPressedFlag = false;
  interrupts();
  return result;                    // Return previous flag state
}
