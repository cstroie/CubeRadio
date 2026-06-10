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

#include <WiFi.h>
#include "display.h"
#include "main.h"
// Spleen fonts https://www.onlinewebfonts.com/icon
#include "Spleen6x12.h" 
#include "Spleen8x16.h" 
#include "Spleen16x32.h"

extern Config config;

/**
 * @brief Array of display type names
 * 
 * Human-readable names for each display type, indexed by display_t values
 */
char* displayNames[OLED_COUNT] = {
    (char*)"128x64 (4 lines)", 
    (char*)"128x32 (2 lines)", 
    (char*)"128x32 (3 lines)"
};

/**
 * @brief Array of display dimensions
 * 
 * Width and height for each display type, indexed by display_t values
 */
int displaySizes[OLED_COUNT][2] = {
    {128, 64}, 
    {128, 32}, 
    {128, 32}
};

/**
 * @brief Layout for logo display
 * 
 * Predefined layout for different display types and logo position
 * Format: [display_type][line_number] where line_number 0-1
 * Value of -1 indicates line is not used for that display type
 */
int logoLayout[OLED_COUNT][2] = {
    {40, -1},     // 128x64 
    {28, -1},     // 128x32
    {28, -1}      // 128x32 small font
};

/**
 * @brief Layout for update display
 * 
 * Predefined layout for different display types and lines
 * Format: [display_type][line_number] where line_number 0-3
 * Value of -1 indicates line is not used for that display type
 */
int updateLayout[OLED_COUNT][4] = {
    {12, 30, 45, 62},     // 128x64 
    {12, -1, 28, -1},     // 128x32
    {12, -1, 22, 31}      // 128x32 small font
};

/**
 * @brief Layout for status display
 * 
 * Predefined layout for different display types and lines in status mode
 * Format: [display_type][line_number] where line_number 0-3
 * Value of -1 indicates line is not used for that display type
 */
int statusLayout[OLED_COUNT][4] = {
    {12, 30, 45, 62},     // 128x64 
    {12, -1, 28, -1},     // 128x32
    {12, -1, 22, 31}      // 128x32 small font
};


/**
 * @brief Construct a new Display object
 * 
 * Initializes the display with a reference to an Adafruit_SSD1306 instance,
 * sets the display to on state, and initializes the activity time to 0.
 * 
 * @param display Reference to Adafruit_SSD1306 display instance
 * @param displayTypeEnum Type of display being used
 */
Display::Display(Adafruit_SSD1306& display, enum display_t displayTypeEnum) : 
    displayRef(display), displayOn(true), lastActivityTime(0), displayType(displayTypeEnum) {}

/**
 * @brief Initialize the display
 * 
 * Configures and initializes the OLED display with default settings,
 * including font, text color, and initial "CubeRadio" splash screen.
 * This method must be called before any other display operations.
 */
void Display::begin() {
  displayRef.begin(SSD1306_SWITCHCAPVCC, config.display_address);
  showLogo();
}

/**
 * @brief Clear the display
 * 
 * Clears the display buffer and updates the physical display.
 * This method immediately clears the screen regardless of display state.
 */
void Display::clear() {
  displayRef.clearDisplay();
  displayRef.display();
}

/**
 * @brief Print text at specified position with alignment
 * @param text The text to print
 * @param x The x-coordinate for the text
 * @param y The y-coordinate for the text
 * @param align The alignment of the text ('l' for left, 'c' for center, 'r' for right)
 * 
 * This function prints text at the specified position with the given alignment.
 * It automatically selects the appropriate font based on available vertical space
 * and handles text alignment (left, center, right). The function maintains a 
 * lastY position to track vertical spacing and font selection.
 */
void Display::printAt(const char* text, int x, int y, char align = 'l') {
    // Text bounds variables for alignment calculations
    int16_t x1, y1;
    uint16_t w, h;
    // Check if we are printing on a new line
    if (y != lastY) {
        // Printing above the last line: new screen, reset lastY
        if (y < lastY) { lastY = 0; }
        // Get the available vertical space
        int v = y - lastY;
        // Choose the adequate font for the available space
        if      (v >= 28) { displayRef.setFont(&Spleen16x32); }
        else if (v >= 12) { displayRef.setFont(&Spleen8x16);  }
        else if (v >= 8)  { displayRef.setFont(&Spleen6x12);  }
        else              { displayRef.setFont(&Spleen6x12);  }
    }
    // Check alignment
    if (align == 'c' or align == 'r') {
        // Compute the text length
        displayRef.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
    }
    if (align == 'c') {
        // Centered
        x = (displayRef.width() - w) / 2;
    }
    if (align == 'r') {
        // Right aligned
        x = displayRef.width() - w - 1;
    }
    if (x < 0) x = 0;
       // Set the cursor position
    displayRef.setCursor(x, y);
    // Print the text
    displayRef.print(text);
    // Keep the last Y
    lastY = y;
}

/**
 * @brief Print text at specified position with alignment
 * @param text The text to print (as a String)
 * @param x The x-coordinate for the text
 * @param y The y-coordinate for the text
 * @param align The alignment of the text ('l' for left, 'c' for center, 'r' for right) 
 */
/**
 * @brief Print text at specified position with alignment
 * @param text The text to print (as a String)
 * @param x The x-coordinate for the text
 * @param y The y-coordinate for the text
 * @param align The alignment of the text ('l' for left, 'c' for center, 'r' for right)
 * 
 * This function prints text at the specified position with the given alignment.
 * It automatically selects the appropriate font based on available vertical space
 * and handles text alignment (left, center, right). The function maintains a 
 * lastY position to track vertical spacing and font selection.
 */
void Display::printAt(const String text, int x, int y, char align) {
    printAt(text.c_str(), x, y, align);
}

/**
 * @brief Show the CubeRadio logo on display
 * 
 * Displays the "CubeRadio" logo centered on the screen using the predefined
 * logo layout for the current display type.
 */
void Display::showLogo() {
    // Clear the buffer
    displayRef.clearDisplay();
    displayRef.setTextColor(SSD1306_WHITE);
    printAt("CubeRadio", 0, logoLayout[displayType][0], 'c');
    // Show the buffer
    displayRef.display();
}

/**
 * @brief Update display with current playback information
 * 
 * Updates the display with current playback status, stream information,
 * volume level, and IP address. Implements scrolling text for long titles.
 * 
 * Scrolling Text Implementation:
 * - For titles longer than 14 characters, implements smooth scrolling
 * - Uses a static offset that increments every 500ms
 * - Displays "title ~~~ title" to create seamless scrolling effect
 * - Calculates pixel positioning for smooth character-by-character scroll
 * 
 * @param isPlaying Current playback state
 * @param streamTitle Current stream title
 * @param streamName Current stream name
 * @param volume Current volume level (0-22)
 * @param bitrate Current stream bitrate
 * @param ipString IP address to display
 */
void Display::update(bool isPlaying, const char* streamTitle, const char* streamName, 
                     int volume, int bitrate, const String& ipString) {
    // If display is off, don't update to save power
    if (!displayOn) {
        return;
    }
    // Clear the display buffer for fresh content
    displayRef.clearDisplay();
    displayRef.setTextColor(SSD1306_WHITE);
    // Different layouts for playing vs stopped
    if (isPlaying) {
        // Fixed '>' character to indicate playing state
        printAt(">", 0, updateLayout[displayType][0], 'l');
        // Display stream title (first line) with scrolling for long titles
        String title = String(streamTitle);
        // Scroll title if too long for display (excluding the '>' character)
        // 16 chars fit on a 128px display with '>' and some margin
        // Calculate how many characters we can display (14 chars = 84 pixels)
        int maxDisplayChars = SCROLL_TEXT_MAX_CHARS;
        if (title.length() > maxDisplayChars) {
            // Static variables for scrolling state management
            static unsigned long lastTitleScrollTime = 0;
            static int titleScrollOffset = 0;
            static String titleScrollText = "";
            // Reset scroll if text changed
            if (titleScrollText != title) {
                titleScrollText = title;
                titleScrollOffset = 0;
            }
            // Scroll every 500ms for smooth animation. The cycle length is
            // title plus the 5-char " ~~~ " separator; wrapping at +4 ended
            // the loop one character early and made the text jump at the seam.
            if (millis() - lastTitleScrollTime > 500) {
                titleScrollOffset = (titleScrollOffset + 1) % (title.length() + 5);
                lastTitleScrollTime = millis();
            }
            // Display scrolled text
            String displayText = title + " ~~~ " + title;
            // Create a temporary string that's long enough to fill the display
            String tempText = displayText + displayText;  // Double it to ensure enough content
            printAt(tempText.substring(titleScrollOffset, titleScrollOffset + maxDisplayChars), 16, updateLayout[displayType][0], 'l');
        } else {
            // Display title without scrolling for short titles
            printAt(title, 16, updateLayout[displayType][0], 'l');
        }
        // Display stream name on second line if available and display supports it
        if (updateLayout[displayType][1] > 0) {
            // Display stream name (second line) - truncated if too long
            String stationName = String(streamName);
            if (stationName.length() > 16) {
                printAt(stationName.substring(0, 16), 0, updateLayout[displayType][1], 'l');
            } else {
                printAt(stationName, 0, updateLayout[displayType][1], 'l');
            }
        }
        // Display volume and bitrate on third line if display supports it
        if (updateLayout[displayType][2] > 0) {
            // Display volume and bitrate on third line
            char volStr[20];
            sprintf(volStr, "Vol %2d", volume);
            printAt(volStr, 0, updateLayout[displayType][2], 'l');
            // Display bitrate on the same line if available
            if (bitrate > 0) {
                char bitrateStr[20];
                sprintf(bitrateStr, "%3d kbps", bitrate);
                printAt(bitrateStr, 0, updateLayout[displayType][2], 'r');
            }
        }
        // Display IP address on last line if display supports it
        if (updateLayout[displayType][3] > 0) {
            // Display IP address on the last line, centered
            printAt(ipString, 0, updateLayout[displayType][3], 'c');
        }
    } else {
        // Display when stopped
        int lineStream = 0;
        if (updateLayout[displayType][1] > 0) {
            printAt("CubeRadio", 0, updateLayout[displayType][0], 'c');
            lineStream = 1;
        }
        // Display current stream name (or the selected playlist item, which
        // the caller passes in streamName when nothing is playing)
        if (strlen(streamName) > 0) {
            String currentStream = String(streamName);
            if (currentStream.length() > 16) {
                printAt(currentStream.substring(0, 16), 0, updateLayout[displayType][lineStream], 'l');
            } else {
                printAt(currentStream, 0, updateLayout[displayType][lineStream], 'l');
            }
        } else {
            printAt("No stream", 0, updateLayout[displayType][lineStream], 'c');
        }
        // Display volume on third line (only for displays with sufficient height)
        if (updateLayout[displayType][2] > 0) {
            // Display volume level
            char volStr[20];
            sprintf(volStr, "Vol %2d", volume);
            printAt(volStr, 0, updateLayout[displayType][2], 'l');
            // Display WiFi RSSI when stopped (only if WiFi is connected)
            if (WiFi.status() == WL_CONNECTED) {
                char rssiStr[20];
                sprintf(rssiStr, "%d dBm", WiFi.RSSI());
                printAt(rssiStr, 0, updateLayout[displayType][2], 'r');
            }
        }
        // Display IP address on last line if display supports it
        if (updateLayout[displayType][3] > 0) {
            // Display IP address on the last line, centered
            printAt(ipString, 0, updateLayout[displayType][3], 'c');
        }
    }
    // Send buffer to display to make changes visible
    displayRef.display();
}

/**
 * @brief Show status information on display
 * 
 * Displays a standardized status screen with "CubeRadio" title and
 * up to three lines of additional information.
 * 
 * @param line1 First line of information (displayed at y=30)
 * @param line2 Second line of information (displayed at y=45)
 * @param line3 Third line of information (displayed at y=62)
 */
void Display::showStatus(const String& line1, const String& line2, const String& line3) {
    // Clear the buffer
    displayRef.clearDisplay();
    displayRef.setTextColor(SSD1306_WHITE);
    // Different modes for different display sizes
    if (displayType == OLED_128x64) {
        printAt("CubeRadio", 0, statusLayout[displayType][0], 'c');
        // Display each line if it has content
        if (line1.length() > 0) {
            printAt(line1, 0, statusLayout[displayType][1], 'l');
        }
        if (line2.length() > 0) {
            printAt(line2, 0, statusLayout[displayType][2], 'l');
        }
        if (line3.length() > 0) {
            printAt(line3, 0, statusLayout[displayType][3], 'l');
        }
    } else {
        // Display first line
        if (line1.length() > 0) {
            printAt(line1, 0, statusLayout[displayType][0], 'l');
        }
        // Display the second or third line if they have content
        if (line2.length() > 0) {
            printAt(line2, 0, statusLayout[displayType][2], 'l');
        } else if (line3.length() > 0) {
            printAt(line3, 0, statusLayout[displayType][2], 'l');
        }
    }
    // Show the buffer
    displayRef.display();
}

/**
 * @brief Turn display on
 * 
 * Enables display output and refreshes the screen.
 * This method immediately turns the display on and shows current content.
 */
void Display::turnOn() {
    displayOn = true;
    displayRef.display();
}

/**
 * @brief Turn display off
 * 
 * Disables display output and clears the screen.
 * This method immediately turns the display off to save power.
 */
void Display::turnOff() {
    displayOn = false;
    displayRef.clearDisplay();
    displayRef.display();
}

/**
 * @brief Check if display is on
 * 
 * @return true if display is currently on
 * @return false if display is currently off
 */
bool Display::isOn() const {
    return displayOn;
}

/**
 * @brief Handle display timeout
 * 
 * Manages automatic display power management based on playback state
 * and user activity. Turns display off after configurable seconds of inactivity
 * when not playing, and keeps it on during playback.
 * 
 * Timeout Logic:
 * - When playing: Display stays on, activity time updated every 5 seconds
 * - When stopped: Display turns off after configurable seconds of inactivity
 * - Handles millis() overflow by resetting activity time
 * 
 * @param isPlaying Current playback state
 * @param currentTime Current system time in milliseconds
 */
void Display::handleTimeout(bool isPlaying, unsigned long currentTime) {
    extern Config config;
    // A timeout of 0 means "never turn off" — without this guard the display
    // blanked on the first loop tick after playback stopped
    if (config.display_timeout <= 0) {
        return;
    }
    const unsigned long DISPLAY_TIMEOUT = config.display_timeout * 1000; // Convert seconds to milliseconds
    // Handle potential millis() overflow by resetting activity time
    if (currentTime < lastActivityTime) {
        lastActivityTime = currentTime; // Reset on overflow
    }
    // If we're playing, keep the display on
    if (isPlaying) {
        // Update activity time periodically during playback to prevent timeout
        static unsigned long lastPlaybackActivityTime = 0;
        if (currentTime - lastPlaybackActivityTime > 5000) { // Every 5 seconds
            lastActivityTime = currentTime;
            lastPlaybackActivityTime = currentTime;
        }
        if (!displayOn) {
            displayOn = true;
            displayRef.display(); // Turn display back on
        }
        return;
    }
    // If we're not playing, check for timeout
    if (currentTime - lastActivityTime > DISPLAY_TIMEOUT) {
        if (displayOn) {
            displayOn = false;
            displayRef.clearDisplay();
            displayRef.display(); // Update display to clear it
        }
    }
}

/**
 * @brief Set last activity time
 * 
 * Updates the timestamp of the last user activity, used for timeout
 * management.
 * 
 * @param time Timestamp in milliseconds
 */
void Display::setActivityTime(unsigned long time) {
    lastActivityTime = time;
    // Also ensure the display is on when there's activity
    this->turnOn();
}

/**
 * @brief Get last activity time
 * 
 * @return unsigned long Timestamp of last user activity
 */
unsigned long Display::getLastActivityTime() const {
    return lastActivityTime;
}

/**
 * @brief Get the number of configured display types
 * 
 * @return int Number of display types
 */
int getDisplayTypeCount() {
    return OLED_COUNT;
}

/**
 * @brief Get the name of a display type by index
 * 
 * @param index Display type index
 * @return const char* Name of the display type, or nullptr if index is invalid
 */
const char* getDisplayTypeName(int index) {
    if (index >= 0 && index < OLED_COUNT) {
        return displayNames[index];
    }
    return nullptr;
}

/**
 * @brief Get the size of a display type by index
 * 
 * @param index Display type index
 * @param width Pointer to store display width
 * @param height Pointer to store display height
 * @return true if successful, false if index is invalid
 */
bool getDisplaySize(int index, int* width, int* height) {
    if (index >= 0 && index < OLED_COUNT) {
        *width = displaySizes[index][0];
        *height = displaySizes[index][1];
        return true;
    }
    return false;
}
