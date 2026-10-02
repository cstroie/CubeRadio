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

#include "pins.h"
#include "main.h"
#include "mpd.h"
#include "display.h"
#include "rotary.h"
#include "player.h"
#include "playlist.h"
#ifndef DISABLE_TOUCH
#include "touch.h"
#endif

// Spleen fonts https://www.onlinewebfonts.com/icon
#include "Spleen6x12.h" 
#include "Spleen8x16.h" 
#include "Spleen16x32.h"
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <stdarg.h>


// Global variables definitions
char ssid[MAX_WIFI_NETWORKS][64] = {""};
char password[MAX_WIFI_NETWORKS][64] = {""};
int wifiNetworkCount = 0;
WebServer server(80);
WebSocketsServer webSocket(81);
WiFiServer mpdServer(6600);
const char* BUILD_TIME = __DATE__ "T" __TIME__"Z";




Adafruit_SSD1306* displayOLED;
Display* display;
RotaryEncoder rotaryEncoder;
TaskHandle_t audioTaskHandle = NULL;

Player player;
 
// Touch buttons
#ifndef DISABLE_TOUCH
TouchButton* touchPlay = nullptr;
TouchButton* touchNext = nullptr;
TouchButton* touchPrev = nullptr;
#endif

// Flag to indicate board button press
static volatile bool boardButtonPressed = false;
// Flag set by audio callbacks (core 0) to request display+status update on core 1
static volatile bool pendingCallbackUpdate = false;

// MPD Interface instance
MPDInterface mpdInterface(mpdServer, player);

// Configuration structure definition
Config config = {
  DEFAULT_I2S_DOUT,
  DEFAULT_I2S_BCLK,
  DEFAULT_I2S_LRC,
  DEFAULT_LED_PIN,
  DEFAULT_ROTARY_CLK,
  DEFAULT_ROTARY_DT,
  DEFAULT_ROTARY_SW,
  DEFAULT_BOARD_BUTTON,
  DEFAULT_DISPLAY_SDA,
  DEFAULT_DISPLAY_SCL,
  0, // Default display type (OLED_128x64)
  DEFAULT_DISPLAY_ADDR,
  30, // Default display timeout (30 seconds)
  DEFAULT_TOUCH_PLAY,
  DEFAULT_TOUCH_NEXT,
  DEFAULT_TOUCH_PREV,
  DEFAULT_TOUCH_THRESHOLD,
  DEFAULT_TOUCH_DEBOUNCE
};



/**
 * @brief Audio stream title callback function
 * This function is called by the Audio library when stream title information is available
 * @param info Pointer to the stream title information
 */
void audio_showstreamtitle(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("Stream title: ");
    Serial.println(info);
    if (strcmp(player.getStreamTitle(), info) != 0) {
      player.setStreamTitle(info);
      pendingCallbackUpdate = true;
    }
  }
}

/**
 * @brief Audio station name callback function
 * This function is called by the Audio library when station name information is available
 * @param info Pointer to the station name information
 */
void audio_showstation(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("Station name: ");
    Serial.println(info);
    if (strcmp(player.getStreamName(), info) != 0) {
      player.setStreamName(info);
      pendingCallbackUpdate = true;
    }
  }
}

/**
 * @brief Audio bitrate callback function
 * This function is called by the Audio library when bitrate information is available
 * @param info Pointer to the bitrate information
 */
void audio_bitrate(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("Bitrate: ");
    Serial.println(info);
    int newBitrate = atoi(info) / 1000;
    if (newBitrate > 0 && newBitrate != player.getBitrate()) {
      player.setBitrate(newBitrate);
      pendingCallbackUpdate = true;
    }
  }
}

/**
 * @brief Audio info callback function
 * This function is called by the Audio library when general audio information is available
 * @param info Pointer to the audio information
 */
void audio_info(const char *info) {
  if (!info || strlen(info) == 0) return;
  Serial.print("Audio Info: ");
  Serial.println(info);
  // Parse StreamUrl= without heap-allocating String objects
  if (strncmp(info, "StreamUrl=", 10) == 0) {
    const char* urlPart = info + 10;
    size_t len = strlen(urlPart);
    // Strip surrounding quotes (single or double)
    if (len >= 2 &&
        ((urlPart[0] == '"' && urlPart[len - 1] == '"') ||
         (urlPart[0] == '\'' && urlPart[len - 1] == '\''))) {
      urlPart++;
      len -= 2;
    }
    // Check for image extensions by inspecting the tail
    bool isImage = (len > 4) &&
      (strncasecmp(urlPart + len - 4, ".png",  4) == 0 ||
       strncasecmp(urlPart + len - 4, ".jpg",  4) == 0 ||
       strncasecmp(urlPart + len - 4, ".ico",  4) == 0 ||
       (len > 5 && strncasecmp(urlPart + len - 5, ".jpeg", 5) == 0));
    if (isImage) {
      // Copy into a null-terminated buffer (urlPart may not be null-terminated after advancing)
      char iconUrl[256];
      size_t copy = (len < sizeof(iconUrl) - 1) ? len : sizeof(iconUrl) - 1;
      strncpy(iconUrl, urlPart, copy);
      iconUrl[copy] = '\0';
      player.setStreamIconUrl(iconUrl);
      Serial.print("Cover image URL: ");
      Serial.println(player.getStreamIconUrl());
      pendingCallbackUpdate = true;
    }
  }
}

/**
 * @brief Audio ICY URL callback function
 * This function is called by the Audio library when ICY URL information is available
 * @param info Pointer to the ICY URL information
 */
void audio_icyurl(const char *info) {
  if (info && strlen(info) > 0) {
    // Only logged: nothing on the device or in the web UI uses the station
    // homepage, so it is not kept in RAM
    Serial.print("ICY URL: ");
    Serial.println(info);
  }
}

/**
 * @brief Audio ICY description callback function
 * This function is called by the Audio library when ICY description information is available
 * @param info Pointer to the ICY description information
 */
void audio_icydescription(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("ICY Description: ");
    Serial.println(info);
  }
}

/**
 * @brief Audio ID3 data callback function
 * This function is called by the Audio library when ID3 data is available
 * @param info Pointer to the ID3 data
 */
void audio_id3data(const char *info) {
  if (info && strlen(info) > 0) {
    Serial.print("ID3 Data: ");
    Serial.println(info);
  }
}

void audio_eof_stream(const char *info) {
  Serial.print("Stream ended: ");
  Serial.println(info ? info : "");
  // Accumulate play time while still on core 0 (all atomic ops)
  if (player.getPlayStartTime() > 0) {
    player.addPlayTime((millis() - player.getPlayStartTime()) / 1000);
    player.setPlayStartTime(0);
    player.setDirty();
  }
  player.setPlaying(false);
  if (config.led_pin >= 0) digitalWrite(config.led_pin, LOW);
  // Defer display/WebSocket update to main loop (core 1)
  pendingCallbackUpdate = true;
}

void audio_error_on_connect(const char *info) {
  Serial.print("Audio connection error: ");
  Serial.println(info ? info : "");
  // Accumulate play time like audio_eof_stream does, so a stale
  // playStartTime can't inflate totalPlayTime later
  if (player.getPlayStartTime() > 0) {
    player.addPlayTime((millis() - player.getPlayStartTime()) / 1000);
    player.setPlayStartTime(0);
    player.setDirty();
  }
  player.setPlaying(false);
  if (config.led_pin >= 0) digitalWrite(config.led_pin, LOW);
  pendingCallbackUpdate = true;
}


/**
 * @brief Read JSON file from SPIFFS
 * Helper function to read and parse JSON files from SPIFFS
 * @param filename Path to the file in SPIFFS
 * @param maxFileSize Maximum allowed file size
 * @param doc JsonDocument to populate with parsed data
 * @return true if successful, false otherwise
 */
bool readJsonFile(const char* filename, size_t maxFileSize, JsonDocument& doc) {
  // Check if the file exists
  if (!SPIFFS.exists(filename)) {
    Serial.printf("JSON file not found: %s\n", filename);
    return false;
  }
  // Open the file
  File file = SPIFFS.open(filename, "r");
  if (!file) {
    Serial.printf("Failed to open JSON file: %s\n", filename);
    return false;
  }
  // Get the size of the file
  size_t size = file.size();
  if (size > maxFileSize) {
    Serial.printf("JSON file too large: %s\n", filename);
    file.close();
    return false;
  }
  // Check if the file is empty
  if (size == 0) {
    Serial.printf("JSON file is empty: %s\n", filename);
    file.close();
    return false;
  }
  // Parse straight from the file: no intermediate copy of its content
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.printf("Failed to parse JSON file %s: %s\n", filename, error.c_str());
    return false;
  }
  // Successfully read and parsed the JSON file
  return true;
}

/**
 * @brief Write JSON file to SPIFFS
 * Helper function to serialize and write JSON files to SPIFFS
 * @param filename Path to the file in SPIFFS
 * @param doc JsonDocument to serialize
 * @return true if successful, false otherwise
 */
bool writeJsonFile(const char* filename, JsonDocument& doc) {
  // Create backup of existing file
  String backupFilename = String(filename) + ".bak";
  if (SPIFFS.exists(filename)) {
    if (SPIFFS.exists(backupFilename)) {
      SPIFFS.remove(backupFilename);
    }
    if (!SPIFFS.rename(filename, backupFilename)) {
      Serial.printf("Error: Failed to create backup of %s, aborting write\n", filename);
      return false;
    }
  }
  // Open the file for writing
  File file = SPIFFS.open(filename, "w");
  if (!file) {
    Serial.printf("Failed to open JSON file for writing: %s\n", filename);
    // Try to restore from backup
    if (SPIFFS.exists(backupFilename)) {
      if (SPIFFS.rename(backupFilename, filename)) {
        Serial.printf("Restored %s from backup\n", filename);
      } else {
        Serial.printf("Error: Failed to restore %s from backup\n", filename);
      }
    }
    return false;
  }
  // Serialize the JSON document to the file
  size_t bytesWritten = serializeJson(doc, file);
  file.close();
  // Check for write failure: empty doc intentionally produces "{}" or "[]" (≥2 bytes),
  // so bytesWritten == 0 reliably means the stream errored before writing anything.
  // A partial write (disk full mid-stream) produces a truncated file — detect it by
  // comparing written bytes against a fresh measureJson() result.
  if (bytesWritten == 0 || bytesWritten != measureJson(doc)) {
    Serial.printf("Failed to write JSON to file: %s (wrote %u of %u bytes)\n",
                  filename, bytesWritten, measureJson(doc));
    // Try to restore from backup
    if (SPIFFS.exists(backupFilename)) {
      SPIFFS.remove(filename); // Remove the failed/partial file
      if (SPIFFS.rename(backupFilename, filename)) {
        Serial.printf("Restored %s from backup\n", filename);
      } else {
        Serial.printf("Error: Failed to restore %s from backup\n", filename);
      }
    }
    return false;
  }
  // Remove backup file after successful save
  if (SPIFFS.exists(backupFilename)) {
    SPIFFS.remove(backupFilename);
  }
  // Successfully wrote the JSON file
  return true;
}

/**
 * @brief Send JSON response with status and message
 * Helper function to send standardized JSON responses
 * @param status Status string ("success" or "error")
 * @param message Human-readable message
 * @param code HTTP status code (default 200 for success, 400 for error)
 */
void sendJsonResponse(const String& status, const String& message, int code = -1) {
  // If code not specified, determine based on status
  if (code == -1) {
    code = (status == "success") ? 200 : 400;
  }
  // Create JSON response
  JsonDocument doc;
  doc["status"] = status;
  doc["message"] = message;
  String json;
  serializeJson(doc, json);
  server.send(code, "application/json", json);
}


/**
 * @brief Handle WiFi configuration API request
 * Returns the current WiFi configuration as JSON
 * This function provides the list of configured WiFi networks in JSON format
 */
void handleWiFiConfig() {
  // Yield to other tasks before processing
  yield();
  // Create JSON document with appropriate size
  JsonDocument doc;
  // Create JSON array
  JsonArray array = doc.to<JsonArray>();
  // Populate JSON array with configured network SSIDs
  for (int i = 0; i < wifiNetworkCount; i++) {
    array.add(String(ssid[i]));
    // Yield to other tasks during long operations
    yield();
  }
  // Serialize JSON to string
  String json;
  serializeJson(array, json);
  // Send the JSON response
  server.send(200, "application/json", json);
  // Yield to other tasks after processing
  yield();
}

/**
 * @brief Handle WiFi network scan
 * Returns a list of available WiFi networks as JSON
 * This function scans for available WiFi networks and returns them along with
 * the list of already configured networks
 */
void handleWiFiScan() {
  // Yield to other tasks before processing
  yield();
  // Create JSON document with appropriate size
  JsonDocument doc;
  // Scan for available networks
  int n = WiFi.scanNetworks();
  yield();
  // Add available networks
  JsonArray networks = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; ++i) {
    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    // Yield to other tasks during long operations
    yield();
  }
  // Add configured networks
  JsonArray configured = doc["configured"].to<JsonArray>();
  for (int i = 0; i < wifiNetworkCount; i++) {
    configured.add(String(ssid[i]));
    // Yield to other tasks during long operations
    yield();
  }
  // Serialize JSON to string
  String json;
  serializeJson(doc, json);
  // Send the JSON response
  server.send(200, "application/json", json);
  // Yield to other tasks after processing
  yield();
}

/**
 * @brief Handle WiFi configuration save
 * Saves WiFi credentials to SPIFFS
 * This function receives WiFi credentials via HTTP POST and saves them to wifi.json
 * It supports both single network and multiple network configurations
 */
void handleWiFiSave() {
  if (!server.hasArg("plain")) {
    sendJsonResponse("error", "Missing JSON data");
    return;
  }
  // Parse JSON data
  String json = server.arg("plain");
  JsonDocument doc;
  // Reject oversized bodies (JsonDocument grows unbounded)
  if (json.length() > 2048) {
    sendJsonResponse("error", "Request body too large", 413);
    return;
  }
  DeserializationError error = deserializeJson(doc, json);
  // Check for errors
  if (error) {
    sendJsonResponse("error", "Invalid JSON");
    return;
  }
  // A non-array body would skip the parse loop below and commit an empty
  // list, silently erasing every saved network
  if (!doc.is<JsonArray>()) {
    sendJsonResponse("error", "JSON root must be an array");
    return;
  }

  // Load existing credentials to preserve passwords when not provided
  char existingSsid[MAX_WIFI_NETWORKS][64] = {""};
  char existingPassword[MAX_WIFI_NETWORKS][64] = {""};
  int existingNetworkCount = 0;
  
  // Parse existing JSON file
  JsonDocument existingDoc;
  if (readJsonFile("/wifi.json", 2048, existingDoc) && existingDoc.is<JsonArray>()) {
    JsonArray existingNetworks = existingDoc.as<JsonArray>();
    for (JsonObject existingNetwork : existingNetworks) {
      if (existingNetworkCount >= MAX_WIFI_NETWORKS) break;
      if (!existingNetwork["ssid"].isNull()) {
        const char* ssidValue = existingNetwork["ssid"];
        if (ssidValue) {
          strncpy(existingSsid[existingNetworkCount], ssidValue, sizeof(existingSsid[existingNetworkCount]) - 1);
          existingSsid[existingNetworkCount][sizeof(existingSsid[existingNetworkCount]) - 1] = '\0';
        }
        if (!existingNetwork["password"].isNull()) {
          const char* pwdValue = existingNetwork["password"];
          if (pwdValue) {
            strncpy(existingPassword[existingNetworkCount], pwdValue, sizeof(existingPassword[existingNetworkCount]) - 1);
            existingPassword[existingNetworkCount][sizeof(existingPassword[existingNetworkCount]) - 1] = '\0';
          } else {
            existingPassword[existingNetworkCount][0] = '\0';
          }
        } else {
          existingPassword[existingNetworkCount][0] = '\0';
        }
        existingNetworkCount++;
      }
    }
  }
  
  // Parse the new networks into local buffers first, so a validation error
  // mid-array cannot leave the global credentials half-overwritten
  char newSsid[MAX_WIFI_NETWORKS][64] = {""};
  char newPassword[MAX_WIFI_NETWORKS][64] = {""};
  int newNetworkCount = 0;
  // Handle the new JSON array format [{"ssid": "name", "password": "pass"}, ...]
  if (doc.is<JsonArray>()) {
    JsonArray networks = doc.as<JsonArray>();
    for (JsonObject network : networks) {
      if (newNetworkCount >= MAX_WIFI_NETWORKS) break;
      // Handle required SSID
      if (!network["ssid"].isNull()) {
        const char* ssidValue = network["ssid"];
        if (ssidValue && strlen(ssidValue) > 0 && strlen(ssidValue) < sizeof(newSsid[newNetworkCount])) {
          strncpy(newSsid[newNetworkCount], ssidValue, sizeof(newSsid[newNetworkCount]) - 1);
          newSsid[newNetworkCount][sizeof(newSsid[newNetworkCount]) - 1] = '\0';
        } else {
          sendJsonResponse("error", "Invalid SSID");
          return;
        }
        // Handle optional password
        if (!network["password"].isNull()) {
          // Use provided password
          const char* pwdValue = network["password"];
          if (pwdValue && strlen(pwdValue) < sizeof(newPassword[newNetworkCount])) {
            strncpy(newPassword[newNetworkCount], pwdValue, sizeof(newPassword[newNetworkCount]) - 1);
            newPassword[newNetworkCount][sizeof(newPassword[newNetworkCount]) - 1] = '\0';
          } else {
            newPassword[newNetworkCount][0] = '\0';
          }
        } else {
          // Look for existing password for this SSID
          for (int i = 0; i < existingNetworkCount; i++) {
            if (strcmp(existingSsid[i], ssidValue) == 0) {
              strncpy(newPassword[newNetworkCount], existingPassword[i], sizeof(newPassword[newNetworkCount]) - 1);
              newPassword[newNetworkCount][sizeof(newPassword[newNetworkCount]) - 1] = '\0';
              break;
            }
          }
        }
        // Increment network count
        newNetworkCount++;
      }
    }
  }
  // All entries valid — commit to the global credentials
  wifiNetworkCount = newNetworkCount;
  for (int i = 0; i < newNetworkCount; i++) {
    strncpy(ssid[i], newSsid[i], sizeof(ssid[i]) - 1);
    ssid[i][sizeof(ssid[i]) - 1] = '\0';
    strncpy(password[i], newPassword[i], sizeof(password[i]) - 1);
    password[i][sizeof(password[i]) - 1] = '\0';
  }
  // Save updated credentials to SPIFFS
  saveWiFiCredentials();
  sendJsonResponse("success", "WiFi configuration saved");
}

/**
 * @brief Handle WiFi status request
 * Returns the current WiFi connection status as JSON
 * This function provides information about the current WiFi connection including
 * connection status, SSID, IP address, and signal strength
 */
void handleWiFiStatus() {
  // Yield to other tasks before processing
  yield();
  // Create JSON document with appropriate size
  JsonDocument doc;
  // Add connection status
  if (WiFi.status() == WL_CONNECTED) {
    doc["connected"] = true;
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
  } else {
    doc["connected"] = false;
  }
  // Serialize JSON to string
  String json;
  serializeJson(doc, json);
  // Send the JSON response
  server.send(200, "application/json", json);
  // Yield to other tasks after processing
  yield();
}

/**
 * @brief Load WiFi credentials from SPIFFS
 * This function reads WiFi credentials from wifi.json in SPIFFS and populates
 * the ssid and password arrays. It supports the new JSON array format.
 */
void loadWiFiCredentials() {
  // Parse the JSON document
  JsonDocument doc;
  if (!readJsonFile("/wifi.json", 2048, doc)) {
    return;
  }
  // Reset count before parsing so stale data from a previous load isn't left behind
  wifiNetworkCount = 0;
  // Handle the JSON array format [{"ssid": "name", "password": "pass"}, ...]
  if (doc.is<JsonArray>()) {
    JsonArray networks = doc.as<JsonArray>();
    // Iterate through each network object
    for (JsonObject network : networks) {
      if (wifiNetworkCount >= MAX_WIFI_NETWORKS) break;
      // Check if SSID exists
      if (!network["ssid"].isNull()) {
        const char* ssidValue = network["ssid"];
        if (ssidValue) {
          strncpy(ssid[wifiNetworkCount], ssidValue, sizeof(ssid[wifiNetworkCount]) - 1);
          ssid[wifiNetworkCount][sizeof(ssid[wifiNetworkCount]) - 1] = '\0';
        } else {
          ssid[wifiNetworkCount][0] = '\0';
        }
        // Check if password exists
        if (!network["password"].isNull()) {
          const char* pwdValue = network["password"];
          if (pwdValue) {
            strncpy(password[wifiNetworkCount], pwdValue, sizeof(password[wifiNetworkCount]) - 1);
            password[wifiNetworkCount][sizeof(password[wifiNetworkCount]) - 1] = '\0';
          } else {
            password[wifiNetworkCount][0] = '\0';
          }
        } else {
          password[wifiNetworkCount][0] = '\0';
        }
        // Increment the network count
        wifiNetworkCount++;
      }
    }
  }
  // Print loaded WiFi credentials
  Serial.println("Loaded WiFi credentials from SPIFFS");
  for (int i = 0; i < wifiNetworkCount; i++) {
    Serial.printf("SSID[%d]: %s\n", i, ssid[i]);
  }
}

/**
 * @brief Save WiFi credentials to SPIFFS
 * This function saves the current WiFi credentials to wifi.json in SPIFFS.
 * It stores networks in the new JSON array format.
 */
void saveWiFiCredentials() {
  JsonDocument doc;
  JsonArray networks = doc.to<JsonArray>();
  // Save networks in the JSON array format [{"ssid": "name", "password": "pass"}, ...]
  for (int i = 0; i < wifiNetworkCount; i++) {
    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = ssid[i];
    if (strlen(password[i]) > 0) {
      network["password"] = password[i];
    }
  }
  // Save the JSON document to SPIFFS using helper function
  if (writeJsonFile("/wifi.json", doc)) {
    Serial.println("Saved WiFi credentials to SPIFFS");
  } else {
    Serial.println("Failed to save WiFi credentials to SPIFFS");
  }
}


/**
 * @brief Extract configuration values from JSON document
 * Helper function to extract configuration values from a JSON document
 * @param doc Reference to the JSON document to extract from
 */
void extractConfigFromJson(JsonDocument& doc) {
  if (!doc["i2s_dout"].isNull()) config.i2s_dout = doc["i2s_dout"];
  if (!doc["i2s_bclk"].isNull()) config.i2s_bclk = doc["i2s_bclk"];
  if (!doc["i2s_lrc"].isNull()) config.i2s_lrc = doc["i2s_lrc"];
  if (!doc["led_pin"].isNull()) config.led_pin = doc["led_pin"];
  if (!doc["rotary_clk"].isNull()) config.rotary_clk = doc["rotary_clk"];
  if (!doc["rotary_dt"].isNull()) config.rotary_dt = doc["rotary_dt"];
  if (!doc["rotary_sw"].isNull()) config.rotary_sw = doc["rotary_sw"];
  if (!doc["board_button"].isNull()) config.board_button = doc["board_button"];
  if (!doc["display_sda"].isNull()) config.display_sda = doc["display_sda"];
  if (!doc["display_scl"].isNull()) config.display_scl = doc["display_scl"];
  if (!doc["display_type"].isNull()) config.display_type = doc["display_type"];
  if (!doc["display_address"].isNull()) config.display_address = doc["display_address"];
  if (!doc["display_timeout"].isNull()) config.display_timeout = doc["display_timeout"];
  if (!doc["touch_play"].isNull()) config.touch_play = doc["touch_play"];
  if (!doc["touch_next"].isNull()) config.touch_next = doc["touch_next"];
  if (!doc["touch_prev"].isNull()) config.touch_prev = doc["touch_prev"];
  if (!doc["touch_threshold"].isNull()) config.touch_threshold = doc["touch_threshold"];
  if (!doc["touch_debounce"].isNull()) config.touch_debounce = doc["touch_debounce"];
}

/**
 * @brief Populate a JSON document with current configuration
 * Helper function to fill a JSON document with configuration values
 * @param doc Reference to the JSON document to populate
 */
void populateConfigJson(JsonDocument& doc) {
  doc["i2s_dout"] = config.i2s_dout;
  doc["i2s_bclk"] = config.i2s_bclk;
  doc["i2s_lrc"] = config.i2s_lrc;
  doc["led_pin"] = config.led_pin;
  doc["rotary_clk"] = config.rotary_clk;
  doc["rotary_dt"] = config.rotary_dt;
  doc["rotary_sw"] = config.rotary_sw;
  doc["board_button"] = config.board_button;
  doc["display_sda"] = config.display_sda;
  doc["display_scl"] = config.display_scl;
  doc["display_type"] = config.display_type;
  doc["display_address"] = config.display_address;
  doc["display_timeout"] = config.display_timeout;
  doc["touch_play"] = config.touch_play;
  doc["touch_next"] = config.touch_next;
  doc["touch_prev"] = config.touch_prev;
  doc["touch_threshold"] = config.touch_threshold;
  doc["touch_debounce"] = config.touch_debounce;
}

/**
 * @brief Load configuration from SPIFFS
 * This function reads configuration from config.json in SPIFFS
 */
void loadConfig() {
  // Parse the JSON document
  JsonDocument doc;
  // Initialize config with default values
  config.i2s_dout = DEFAULT_I2S_DOUT;
  config.i2s_bclk = DEFAULT_I2S_BCLK;
  config.i2s_lrc = DEFAULT_I2S_LRC;
  config.led_pin = DEFAULT_LED_PIN;
  config.rotary_clk = DEFAULT_ROTARY_CLK;
  config.rotary_dt = DEFAULT_ROTARY_DT;
  config.rotary_sw = DEFAULT_ROTARY_SW;
  config.board_button = DEFAULT_BOARD_BUTTON;
  config.display_sda = DEFAULT_DISPLAY_SDA;
  config.display_scl = DEFAULT_DISPLAY_SCL;
  config.display_type = 0;
  config.display_address = DEFAULT_DISPLAY_ADDR;
  config.display_timeout = 30;
  config.touch_play = DEFAULT_TOUCH_PLAY;
  config.touch_next = DEFAULT_TOUCH_NEXT;
  config.touch_prev = DEFAULT_TOUCH_PREV;
  config.touch_threshold = DEFAULT_TOUCH_THRESHOLD;
  config.touch_debounce = DEFAULT_TOUCH_DEBOUNCE;
  // Read configuration from SPIFFS
  if (!readJsonFile("/config.json", 1024, doc)) {
    Serial.println("Config file not found, using defaults");
    // Defaults are saved by setup() after display_type is validated
  } else {
    // Load configuration values, using defaults for missing values
    extractConfigFromJson(doc);
    // Print loaded configuration
    Serial.println("Loaded configuration from SPIFFS");
  }
}

/**
 * @brief Save configuration to SPIFFS
 * This function saves the current configuration to config.json in SPIFFS
 */
void saveConfig() {
  // Create a JSON document
  JsonDocument doc;
  populateConfigJson(doc);
  // Save the JSON document to SPIFFS using helper function
  if (writeJsonFile("/config.json", doc)) {
    Serial.println("Saved configuration to SPIFFS");
  } else {
    Serial.println("Failed to save configuration to SPIFFS");
  }
}

/**
 * @brief Handle GET request for configuration
 * Returns the current configuration as JSON
 * This function serves the current configuration in JSON format.
 */
void handleGetConfig() {
  // Yield to other tasks before processing
  yield();
  // Create JSON document with appropriate size
  JsonDocument doc;
  // Populate JSON document with configuration values
  populateConfigJson(doc);
  // Add display types information
  JsonArray displays = doc["displays"].to<JsonArray>();
  for (int i = 0; i < getDisplayTypeCount(); i++) {
    const char* displayName = getDisplayTypeName(i);
    if (displayName) {
      displays.add(displayName);
    }
  }
  // Serialize JSON to string
  String json;
  serializeJson(doc, json);
  // Return configuration as JSON
  server.send(200, "application/json", json);
  // Yield to other tasks after processing
  yield();
}

/**
 * @brief Handle POST request for configuration
 * Updates the configuration with new JSON data and saves to SPIFFS
 * This function receives a new configuration via HTTP POST, validates it, and saves it to SPIFFS.
 */
void handlePostConfig() {
  // Check for JSON data
  if (!server.hasArg("plain")) {
    sendJsonResponse("error", "Missing JSON data");
    return;
  }
  // Parse the JSON data
  String jsonData = server.arg("plain");
  JsonDocument doc;
  // Reject oversized bodies (JsonDocument grows unbounded)
  if (jsonData.length() > 1024) {
    sendJsonResponse("error", "Request body too large", 413);
    return;
  }
  DeserializationError error = deserializeJson(doc, jsonData);
  // Check for JSON parsing errors
  if (error) {
    sendJsonResponse("error", "Invalid JSON");
    return;
  }
  // Update configuration values
  extractConfigFromJson(doc);
  // Save to SPIFFS
  saveConfig();
  // Return status as JSON
  sendJsonResponse("success", "Configuration updated successfully");
}



/**
 * @brief Audio task function
 * Handles audio streaming on core 0
 * @param pvParameters Task parameters (not used)
 */
// Cooperative pause handshake: the Audio object is not thread-safe, so core 1
// must not call connecttohost()/stopSong() while audio->loop() runs on core 0.
// The task parks itself at a safe point (between loop() calls) when asked.
static volatile bool audioTaskPauseRequested = false;
static volatile bool audioTaskParked = false;

void audioTask(void *pvParameters) {
  while (true) {
    if (audioTaskPauseRequested) {
      // Park at a safe point until the pause is released
      audioTaskParked = true;
      vTaskDelay(1);
      continue;
    }
    audioTaskParked = false;
    // Process audio streaming with error handling
    player.handleAudio();
    // Add a small yield to prevent interrupt blocking
    vTaskDelay(1);  // Better than delay() for FreeRTOS tasks
  }
}

/**
 * @brief Pause the audio task at a safe point
 * Blocks (up to ~100 ms) until the task has parked between loop() calls,
 * so the caller can safely mutate the Audio object from core 1.
 */
void pauseAudioTask() {
  if (audioTaskHandle == NULL) return;
  audioTaskPauseRequested = true;
  // Wait for the task to acknowledge; audio->loop() takes a few ms at most
  for (int i = 0; i < 100 && !audioTaskParked; i++) {
    delay(1);
  }
}

/**
 * @brief Resume the audio task after pauseAudioTask()
 */
void resumeAudioTask() {
  audioTaskPauseRequested = false;
}



/**
 * @brief Interrupt service routine for board button
 * Sets a flag when the board button is pressed
 */
void IRAM_ATTR boardButtonISR() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  // Debounce the button press (ignore if less than 50ms since last press)
  if (interruptTime - lastInterruptTime > 50) {
    boardButtonPressed = true;  // Set flag to indicate button press detected
  }
  lastInterruptTime = interruptTime;  // Update last interrupt time for debouncing
}

/**
 * @brief Handle board button input
 * Processes the built-in button (GPIO 0) for play/stop toggle functionality
 * This function checks for button presses detected by the interrupt handler
 */
void handleBoardButton() {
  // Only handle board button if it's configured (not negative)
  // and not the same as rotary switch
  if (config.board_button < 0 || config.board_button == config.rotary_sw) {
    return;
  }
  // Check if button was pressed (detected by interrupt)
  if (boardButtonPressed) {
    // Toggle play/stop
    if (player.isPlaying()) {
      // If currently playing, stop the stream
      player.stopStream();
    } else {
      // If we have a current stream, resume it
      if (strlen(player.getStreamUrl()) > 0) {
        player.startStream();
      } 
      // Otherwise, if we have playlist items, play the selected one
      else if (player.isPlaylistIndexValid()) {
        player.startStream(player.getCurrentPlaylistItemURL(), player.getCurrentPlaylistItemName());
      }
      // Save player state after starting
      player.savePlayerState();
    }
    // Update display and notify clients
    updateDisplay();
    sendStatusToClients();
    // Clear the flag
    boardButtonPressed = false;
  }
}

/**
 * @brief Handle rotary encoder input
 * Processes rotation and button press events from the rotary encoder
 * This function processes rotary encoder events and controls volume when playing
 * or playlist selection when stopped. It also handles button presses to start/stop playback.
 */
void handleRotary() {
  static int lastRotaryPosition = 0;
  // Check if rotary encoder position has changed
  int currentPosition = rotaryEncoder.getPosition();
  if (currentPosition != lastRotaryPosition) {
    int diff = currentPosition - lastRotaryPosition;
    // Apply the full rotation delta: several detents can accumulate between
    // 150 ms main-loop ticks, and each one should count as a step
    if (player.isPlaying()) {
      // If playing, adjust volume by the delta (setVolume clamps to 0-22)
      player.setVolume(player.getVolume() + diff);
      sendStatusToClients();  // Notify clients of status change
    } else {
      // If not playing, move the playlist selection one step per detent
      for (int i = 0; i < abs(diff); i++) {
        player.setPlaylistIndex(diff > 0 ? player.getNextPlaylistItem()
                                         : player.getPrevPlaylistItem());
      }
    }
    // Update last position
    lastRotaryPosition = currentPosition;  
    // Update activity time on user interaction
    display->setActivityTime(millis()); 
    // Refresh display with new values
    updateDisplay();                      
  }
  // Process button press if detected
  if (rotaryEncoder.wasButtonPressed()) {
    // Update activity time
    display->setActivityTime(millis()); 
    if (player.isPlaying()) {
      // If playing, stop playback
      player.stopStream();
    } else {
      // If we have a current stream, resume it
      if (strlen(player.getStreamUrl()) > 0) {
        player.startStream();
      } 
      // Otherwise, if we have playlist items, play the selected one
      else if (player.isPlaylistIndexValid()) {
        player.startStream(player.getCurrentPlaylistItemURL(), player.getCurrentPlaylistItemName());
      }
      // Save state when user initiates playback
      player.savePlayerState(); 
    }
    // Refresh display
    updateDisplay();
    // Notify clients of status change 
    sendStatusToClients();
  }
}

/**
 * @brief Handle touch button input
 * Processes the touch buttons for play/pause, next/volume-up, and previous/volume-down
 * This function implements the same functionality as the rotary encoder
 */
void handleTouch() {
#ifndef DISABLE_TOUCH
  // Handle play/pause button
  if (touchPlay && touchPlay->wasPressed()) {
    display->setActivityTime(millis()); // Update activity time
    updateDisplay(); // Turn display back on and update
    // Toggle play/stop
    if (player.isPlaying()) {
      // If playing, stop playback
      player.stopStream();
    } else {
      // If we have a current stream, resume it
      if (strlen(player.getStreamUrl()) > 0) {
        player.startStream();
      } 
      // Otherwise, if we have playlist items, play the selected one
      else if (player.isPlaylistIndexValid()) {
        player.startStream(player.getCurrentPlaylistItemURL(), player.getCurrentPlaylistItemName());
      }
      // Save state when user initiates playback
      player.savePlayerState(); 
    }
    // Refresh display and notify clients of status change
    updateDisplay();
    sendStatusToClients();
  }
  // Handle next/volume-up button
  if (touchNext && touchNext->wasPressed()) {
    display->setActivityTime(millis()); // Update activity time
    updateDisplay(); // Turn display back on and update
    if (player.isPlaying()) {
      // If playing, increase volume by 1 (capped at 22)
      player.setVolume(min(22, player.getVolume() + 1));
    } else {
      // If not playing, select next item in playlist
      player.setPlaylistIndex(player.getNextPlaylistItem());
    }
    // Update display and notify clients of status change
    updateDisplay();
    sendStatusToClients();
  }
  // Handle previous/volume-down button
  if (touchPrev && touchPrev->wasPressed()) {
    display->setActivityTime(millis()); // Update activity time
    updateDisplay(); // Turn display back on and update
    if (player.isPlaying()) {
      // If playing, decrease volume by 1 (capped at 0)
      player.setVolume(max(0, player.getVolume() - 1));
    } else {
      // If not playing, select previous item in playlist
      player.setPlaylistIndex(player.getPrevPlaylistItem());
    }
    // Update display and notify clients of status change
    updateDisplay();
    sendStatusToClients();
  }
#endif // DISABLE_TOUCH
}



/**
 * @brief Buffered writer for chunked HTTP responses
 * @details Collects small pieces into a fixed buffer and sends a chunk each
 * time it fills up, so a response is never assembled in RAM. The response is
 * terminated when the writer goes out of scope.
 */
class ChunkedResponse {
public:
  ChunkedResponse(int code, const char* contentType) {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(code, contentType, "");
  }
  ~ChunkedResponse() {
    flush();
    // Terminating zero-length chunk
    server.sendContent("");
  }
  void write(const char* data, size_t len) {
    while (len > 0) {
      if (used == sizeof(buf)) flush();
      size_t n = min(len, sizeof(buf) - used);
      memcpy(buf + used, data, n);
      used += n;
      data += n;
      len -= n;
    }
  }
  void print(const char* text) {
    write(text, strlen(text));
  }
  void printf(const char* format, ...) {
    char tmp[96];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(tmp, sizeof(tmp), format, args);
    va_end(args);
    if (len > 0) write(tmp, min((size_t)len, sizeof(tmp) - 1));
  }
  // Escape text (may come from untrusted stream metadata) for HTML content or attributes
  void printEscaped(const char* text) {
    for (const char* p = text; p && *p; p++) {
      switch (*p) {
        case '&':  print("&amp;");  break;
        case '<':  print("&lt;");   break;
        case '>':  print("&gt;");   break;
        case '"':  print("&quot;"); break;
        case '\'': print("&#39;");  break;
        default:   write(p, 1);     break;
      }
    }
  }
private:
  void flush() {
    if (used > 0) {
      server.sendContent(buf, used);
      used = 0;
    }
  }
  char buf[1024];
  size_t used = 0;
};

/**
 * @brief Handle simple web page request
 * Serves a minimal HTML page for controlling the radio
 * This function provides a simple interface with play/stop controls
 * and stream selection without CSS or JavaScript
 */
void handleSimpleWebPage() {
  if (server.method() == HTTP_POST) {
    // Handle form submission
    if (server.hasArg("action")) {
      String action = server.arg("action");
      // Perform action based on form input
      if (action == "play") {
        // Play selected stream
        if (server.hasArg("stream") && player.getPlaylistCount() > 0) {
          int streamIndex = server.arg("stream").toInt();
          if (streamIndex >= 0 && streamIndex < player.getPlaylistCount()) {
            // Stop playback
            player.stopStream();
            // Play selected stream
            player.setPlaylistIndex(streamIndex);
            player.startStream(player.getCurrentPlaylistItemURL(), player.getCurrentPlaylistItemName());
          }
        } else if (strlen(player.getStreamUrl()) > 0) {
          // Stop current playback
          player.stopStream();
          // Resume current stream
          player.startStream();
        } else if (player.isPlaylistIndexValid()) {
          // Stop playback
          player.stopStream();
          // Play currently selected stream
          player.startStream(player.getCurrentPlaylistItemURL(), player.getCurrentPlaylistItemName());
        }
        // Save player state when user requests to play
        player.savePlayerState();
        // Update display and notify clients
        updateDisplay();
        sendStatusToClients();
      } else if (action == "stop") {
        // Stop playback
        player.stopStream();
        // Update display and notify clients
        updateDisplay();
        sendStatusToClients();
      } else if (action == "volume") {
        // Set volume
        if (server.hasArg("volume")) {
          int newVolume = server.arg("volume").toInt();
          if (newVolume >= 0 && newVolume <= 22) {
            player.setVolume(newVolume);
            // Update display and notify clients
            updateDisplay();
            sendStatusToClients();
          }
        }
      } else if (action == "instant") {
        // Play a stream URL
        if (server.hasArg("url")) {
          String customUrl = server.arg("url");
          if (customUrl.length() > 0 && 
              (customUrl.startsWith("http://") || customUrl.startsWith("https://"))) {
            // Stop current playback
            player.stopStream();
            // Use a generic name for the stream
            String streamName = "Stream";
            player.startStream(customUrl.c_str(), streamName.c_str());
            // Update display and notify clients
            updateDisplay();
            sendStatusToClients();
          }
        }
      }
    }
  }
  
  // Stream the page in chunks: RAM use no longer grows with the playlist
  ChunkedResponse page(200, "text/html");
  page.print("<!DOCTYPE html><html><head><title>CubeRadio</title>");
  page.print("<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/@picocss/pico@2/css/pico.classless.min.css\">");
  page.print("</head><body><header><h1>CubeRadio</h1></header><main>");
  page.print("<section><h2>Status: ");
  page.print(player.isPlaying() ? "PLAY" : "STOP");
  page.print("</h2>");
  // Show current stream name
  if (player.isPlaying() && player.getStreamTitle()[0]) {
    page.print("<p><b>Now playing:</b> ");
    page.printEscaped(player.getStreamTitle());
    page.print("</p>");
  } else if (!player.isPlaying() && player.isPlaylistIndexValid()) {
    page.print("<p><b>Selected:</b> ");
    page.printEscaped(player.getPlaylistItem(player.getPlaylistIndex()).name);
    page.print("</p>");
  }
  page.print("</section><section><h2>Controls</h2>");
  page.print("<form method='post'><fieldset role='group'>");
  page.print("<button name='action' value='play' type='submit'>Play</button> ");
  page.print("<button name='action' value='stop' type='submit'>Stop</button>");
  page.print("</fieldset></form>");
  page.print("<form method='post'><fieldset role='group'>");
  page.print("<select name='volume' id='volume'>");
  for (int i = 0; i <= 22; i++) {
    page.printf("<option value='%d'%s>%d</option>", i, (i == player.getVolume()) ? " selected" : "", i);
  }
  page.print("</select>");
  page.print("<button name='action' value='volume' type='submit'>Set&nbsp;volume</button>");
  page.print("</fieldset></form></section><section><h2>Playlist</h2>");
  // Show stream selection dropdown if we have a playlist
  if (player.getPlaylistCount() > 0) {
    page.print("<form method='post'><fieldset role='group'>");
    page.print("<select name='stream' id='stream'>");
    int selected = player.getPlaylistIndex();
    player.forEachPlaylistItem([&](int i, const StreamInfo& item) {
      page.printf("<option value='%d'%s>", i, (i == selected) ? " selected" : "");
      page.printEscaped(item.name);
      page.print("</option>");
      return true;
    });
    page.print("</select>");
    page.print("<button name='action' value='play' type='submit'>Play&nbsp;selected</button>");
    page.print("</fieldset></form>");
  } else {
    page.print("<p>No streams in playlist.</p>");
  }
  // Add instant play input for custom stream URL even when no playlist
  page.print("<h2>Play instant stream</h2>");
  page.print("<form method='post'><fieldset role='group'>");
  page.print("<input type='url' name='url' id='url' placeholder='http://example.com/stream'>");
  page.print("<button name='action' value='instant' type='submit'>Play&nbsp;stream</button>");
  page.print("</fieldset></form></section></main>");
  page.print("<footer><p>CubeRadio Simple Interface</p></footer></body></html>");
}


/**
 * @brief Handle GET request for streams
 * Streams the playlist as JSON Lines (one {"name","url"} object per line)
 * @details Only entries the device considers valid are sent, so line N of the
 * response is playlist index N. The response is sent in chunks; RAM use does
 * not grow with the playlist.
 */
void handleGetStreams() {
  ChunkedResponse response(200, "application/x-ndjson");
  char line[PLAYLIST_LINE_MAX];
  player.forEachPlaylistItem([&](int, const StreamInfo& item) {
    size_t len = Playlist::formatLine(item, line, sizeof(line) - 1);
    if (len > 0) {
      line[len++] = '\n';
      response.write(line, len);
    }
    return true;
  });
}

/**
 * @brief Receive the body of POST /api/streams
 * @details The JSONL body is streamed straight into a temporary SPIFFS file
 * and validated line by line, so the request is never held in RAM.
 */
void handlePostStreamsUpload() {
  // Multipart form uploads reach this callback too, but without a raw buffer
  String contentType = server.header("Content-Type");
  contentType.toLowerCase();
  if (contentType.startsWith("multipart/")) return;
  HTTPRaw& raw = server.raw();
  switch (raw.status) {
    case RAW_START:
      player.beginPlaylistUpload();
      break;
    case RAW_WRITE:
      player.writePlaylistUpload(raw.buf, raw.currentSize);
      break;
    case RAW_ABORTED:
      player.abortPlaylistUpload();
      break;
    default:
      break;
  }
  yield();
}

/**
 * @brief Handle POST request for streams
 * Replaces the playlist with the uploaded JSON Lines body
 * @details Every line must be a {"name","url"} object with an http(s) URL;
 * blank lines are ignored. On any invalid line the old playlist is kept.
 * An empty playlist is sent as a single blank line.
 */
void handlePostStreams() {
  if (!player.endPlaylistUpload()) {
    sendJsonResponse("error", player.getPlaylistUploadError());
    return;
  }
  // Refresh WebSocket clients and the OLED (it may show the selected name)
  updateDisplay();
  sendStatusToClients();
  sendJsonResponse("success", "Playlist updated successfully");
}

/**
 * @brief Handle player request
 * Controls stream playback (play/stop) or returns player status
 * This function handles HTTP requests to control playback or get player status.
 * For POST requests, it supports both JSON payload and form data with action parameter.
 * For GET requests, it returns player status and stream information.
 * 
 * POST /api/player:
 *   JSON payload: {"action": "play", "url": "...", "name": "...", "index": 0}
 *   JSON payload: {"action": "play", "index": 0}
 *   JSON payload: {"action": "stop"}
 *   Form data: action=play&url=...&name=...&index=0
 *   Form data: action=play&index=0
 *   Form data: action=stop
 * 
 * GET /api/player:
 *   Returns: {"status": "play|stop", "stream": {...}}
 *   When playing: stream object contains name, title, url, index, bitrate, elapsed
 *   When stopped: stream object is omitted
 */
void handlePlayer() {
  // Handle GET request - return player status
  if (server.method() == HTTP_GET) {
    // Create JSON document with appropriate size
    JsonDocument doc;
    // Add player status
    doc["status"] = player.isPlaying() ? "play" : "stop";
    // If playing, add stream information
    if (player.isPlaying()) {
      JsonObject streamObj = doc["stream"].to<JsonObject>();
      streamObj["name"] = player.getStreamName();
      streamObj["title"] = player.getStreamTitle();
      streamObj["url"] = player.getStreamUrl();
      streamObj["index"] = player.getPlaylistIndex();
      streamObj["bitrate"] = player.getBitrate();
      // Calculate elapsed time
      if (player.getPlayStartTime() > 0) {
        // playStartTime is in milliseconds; unsigned subtraction handles rollover
        unsigned long elapsedTime = (millis() - player.getPlayStartTime()) / 1000;
        streamObj["elapsed"] = elapsedTime;
      } else {
        streamObj["elapsed"] = 0;
      }
    }
    // Serialize JSON to string
    String json;
    serializeJson(doc, json);
    // Return status as JSON
    server.send(200, "application/json", json);
    return;
  }
  // Handle POST request - control playback
  String action, url, name;
  int index = -1;
  // Check if request has JSON payload
  if (server.hasArg("plain")) {
    // Handle JSON payload
    String json = server.arg("plain");
    JsonDocument doc;
    // Reject oversized bodies (JsonDocument grows unbounded)
    if (json.length() > 512) {
      sendJsonResponse("error", "Request body too large", 413);
      return;
    }
    DeserializationError error = deserializeJson(doc, json);
    // Check for JSON parsing errors
    if (error) {
      sendJsonResponse("error", "Invalid JSON");
      return;
    }
    // Extract parameters from JSON
    if (!doc["action"].isNull()) {
      action = doc["action"].as<String>();
    }
    if (!doc["url"].isNull()) {
      url = doc["url"].as<String>();
    }
    if (!doc["name"].isNull()) {
      name = doc["name"].as<String>();
    }
    if (!doc["index"].isNull()) {
      index = doc["index"].as<int>();
    }
  } 
  // Check if request has form data
  else if (server.hasArg("action")) {
    // Handle form data
    action = server.arg("action");
    if (server.hasArg("url")) {
      url = server.arg("url");
    }
    if (server.hasArg("name")) {
      name = server.arg("name");
    }
    if (server.hasArg("index")) {
      index = server.arg("index").toInt();
    }
  } 
  else {
    sendJsonResponse("error", "Missing action parameter");
    return;
  }
  // Check for required action parameter
  if (action.length() == 0) {
    sendJsonResponse("error", "Missing required parameter: action");
    return;
  }
  if (action == "play") {
    // Handle case where only index is provided
    if (url.length() == 0 && name.length() == 0 && index >= 0) {
      // Validate index
      if (index >= player.getPlaylistCount()) {
        sendJsonResponse("error", "Invalid playlist index");
        return;
      }
      // Extract stream data from playlist
      url = String(player.getPlaylistItem(index).url);
      name = String(player.getPlaylistItem(index).name);
      player.setPlaylistIndex(index);
    } 
    // Handle case where URL is provided (with optional name)
    else if (url.length() > 0) {
      // If no name provided, check if we have a current stream name
      if (name.length() == 0 && strlen(player.getStreamUrl()) > 0 && url == String(player.getStreamUrl())) {
        name = (strlen(player.getStreamName()) > 0) ? String(player.getStreamName()) : "Unknown Station";
      }
      // Validate URL format
      if (!url.startsWith("http://") && !url.startsWith("https://")) {
        sendJsonResponse("error", "Invalid URL format. Must start with http:// or https://");
        return;
      }
      // Update currentSelection based on URL
      int urlIndex = player.findPlaylistUrl(url.c_str());
      if (urlIndex >= 0) {
        player.setPlaylistIndex(urlIndex);
      }
    }
    // Handle case where we're resuming playback
    else if (url.length() == 0 && strlen(player.getStreamUrl()) > 0) {
      url = String(player.getStreamUrl());
      name = (strlen(player.getStreamName()) > 0) ? String(player.getStreamName()) : "Unknown Station";
    }
    // No valid play parameters
    else {
      sendJsonResponse("error", "Missing required parameters for play action");
      return;
    }
    // Stop any currently playing stream
    player.stopStream();
    // Start the stream
    player.startStream(url.c_str(), name.c_str());
    // Save player state when user requests to play
    player.savePlayerState();
    // Update display and notify clients
    updateDisplay();
    sendStatusToClients();
    // Send success response
    sendJsonResponse("success", "Stream started successfully");
  } 
  else if (action == "stop") {
    // Stop any currently playing stream
    player.stopStream();
    // Update display and notify clients
    updateDisplay();
    sendStatusToClients();
    // Send success response
    sendJsonResponse("success", "Stream stopped successfully");
  } 
  else {
    sendJsonResponse("error", "Invalid action. Supported actions: play, stop");
    return;
  }
}

/**
 * @brief Handle mixer request
 * Gets or sets the volume and tone levels
 * This function handles HTTP requests to get or set the volume and/or tone levels. 
 * For GET requests, it returns the current mixer status as JSON.
 * For POST requests, it supports both JSON payload and form data, validates the input, and updates the settings.
 * 
 * GET /api/mixer:
 *   Returns: {"volume": 11, "bass": 0, "mid": 0, "treble": 0}
 * 
 * POST /api/mixer:
 *   JSON payload: {"volume": 10}
 *   JSON payload: {"bass": 4, "treble": -2}
 *   Form data: volume=10
 *   Form data: bass=4&treble=-2
 */
void handleMixer() {
  // Handle GET request - return current mixer status
  if (server.method() == HTTP_GET) {
    // Create JSON document with appropriate size
    JsonDocument doc;
    // Add mixer status
    doc["volume"] = player.getVolume();
    doc["bass"] = player.getBass();
    doc["mid"] = player.getMid();
    doc["treble"] = player.getTreble();
    // Serialize JSON to string
    String json;
    serializeJson(doc, json);
    // Return status as JSON
    server.send(200, "application/json", json);
    return;
  }
  // Handle POST request - update mixer settings
  JsonDocument doc;
  bool hasData = false;
  // Handle JSON payload
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    // Reject oversized bodies (JsonDocument grows unbounded)
    if (json.length() > 256) {
      sendJsonResponse("error", "Request body too large", 413);
      return;
    }
    DeserializationError error = deserializeJson(doc, json);
    // Check for JSON parsing errors
    if (error) {
      sendJsonResponse("error", "Invalid JSON");
      return;
    }
    hasData = true;
  }
  // Handle form data
  else {
    // Check if any form parameters are present
    if (server.hasArg("volume") || server.hasArg("bass") || 
        server.hasArg("mid") || server.hasArg("treble")) {
      hasData = true;
      // Add form data to JSON document
      if (server.hasArg("volume")) {
        doc["volume"] = server.arg("volume");
      }
      if (server.hasArg("bass")) {
        doc["bass"] = server.arg("bass");
      }
      if (server.hasArg("mid")) {
        doc["mid"] = server.arg("mid");
      }
      if (server.hasArg("treble")) {
        doc["treble"] = server.arg("treble");
      }
    }
  }
  // Check if any data was provided
  if (!hasData) {
    sendJsonResponse("error", "Missing data: volume, bass, mid, or treble");
    return;
  }
  bool toneUpdated = false;
  // Handle volume setting
  if (!doc["volume"].isNull()) {
    int newVolume;
    if (doc["volume"].is<const char*>()) {
      newVolume = atoi(doc["volume"].as<const char*>());
    } else {
      newVolume = doc["volume"];
    }
    // Validate volume range
    if (newVolume < 0 || newVolume > 22) {
      sendJsonResponse("error", "Volume must be between 0 and 22");
      return;
    }
    player.setVolume(newVolume);
  }
  // Handle bass setting
  if (!doc["bass"].isNull()) {
    int newBass;
    if (doc["bass"].is<const char*>()) {
      newBass = atoi(doc["bass"].as<const char*>());
    } else {
      newBass = doc["bass"];
    }
    if (newBass < -6 || newBass > 6) {
      sendJsonResponse("error", "Bass must be between -6 and 6");
      return;
    }
    player.setBass(newBass);
    toneUpdated = true;
  }
  // Handle mid setting
  if (!doc["mid"].isNull()) {
    int newMid;
    if (doc["mid"].is<const char*>()) {
      newMid = atoi(doc["mid"].as<const char*>());
    } else {
      newMid = doc["mid"];
    }
    if (newMid < -6 || newMid > 6) {
      sendJsonResponse("error", "Midrange must be between -6 and 6");
      return;
    }
    player.setMid(newMid);
    toneUpdated = true;
  }
  // Handle treble setting
  if (!doc["treble"].isNull()) {
    int newTreble;
    if (doc["treble"].is<const char*>()) {
      newTreble = atoi(doc["treble"].as<const char*>());
    } else {
      newTreble = doc["treble"];
    }
    if (newTreble < -6 || newTreble > 6) {
      sendJsonResponse("error", "Treble must be between -6 and 6");
      return;
    }
    player.setTreble(newTreble);
    toneUpdated = true;
  }
  // Apply tone settings to audio
  if (toneUpdated) {
    player.setTone();
  }
  // Update display and notify clients
  updateDisplay();
  sendStatusToClients();
  // Send success response
  sendJsonResponse("success", "Mixer settings updated successfully");
}


/**
 * @brief Handle import configuration request
 * Imports a combined JSON configuration file and saves individual files to SPIFFS
 * This function receives a JSON file containing all configurations and decomposes
 * it into individual config.json, wifi.json and player.json files. A
 * "playlist.json" key is ignored; the web UI sends it to /api/streams.
 */
void handleImportConfig() {
  // Check if request method is POST
  if (server.method() != HTTP_POST) {
    sendJsonResponse("error", "Method not allowed", 405);
    return;
  }
  // Check if we have data in the request body
  if (!server.hasArg("plain")) {
    sendJsonResponse("error", "No data received");
    return;
  }
  // Get the JSON data from the request body
  String jsonData = server.arg("plain");
  // Check if data is empty
  if (jsonData.length() == 0) {
    sendJsonResponse("error", "No file uploaded");
    return;
  }
  // Parse the JSON data
  JsonDocument doc;
  // Reject oversized bodies (JsonDocument grows unbounded); config.json,
  // wifi.json and player.json together stay well below 4 KB
  if (jsonData.length() > 4096) {
    sendJsonResponse("error", "Request body too large", 413);
    return;
  }
  DeserializationError error = deserializeJson(doc, jsonData);
  if (error) {
    Serial.printf("Failed to parse uploaded JSON: %s\n", error.c_str());
    sendJsonResponse("error", "Invalid JSON format");
    return;
  }
  // The playlist is not part of the bundle: the web UI uploads it separately
  // through POST /api/streams, which streams it to SPIFFS
  const char* configFiles[] = {"config.json", "wifi.json", "player.json"};
  bool success = true;
  for (int i = 0; i < 3; i++) {
    const char* filename = configFiles[i];
    if (!doc[filename].isNull()) {
      String filePath = String("/") + filename;
      JsonDocument tempDoc;
      tempDoc.set(doc[filename]);
      if (writeJsonFile(filePath.c_str(), tempDoc)) {
        Serial.println("Saved " + String(filename) + " to SPIFFS");
      } else {
        Serial.println("Failed to save " + String(filename) + " to SPIFFS");
        success = false;
      }
      delay(1);
    }
  }
  if (success) {
    sendJsonResponse("success", "Configuration imported successfully");
  } else {
    sendJsonResponse("error", "Error importing configuration", 500);
  }
}


/**
 * @brief Minimal writer for a flat JSON object in a fixed buffer
 * @details Builds the object without heap allocations. A field that does not
 * fit is dropped as a whole, so the output is always valid JSON.
 */
class JsonObjectWriter {
public:
  JsonObjectWriter(char* buffer, size_t capacity) : buf(buffer), cap(capacity) {
    buf[len++] = '{';
  }
  void add(const char* key, const char* value) {
    size_t mark = begin(key);
    put('"');
    for (const char* p = value; p && *p; p++) {
      uint8_t c = static_cast<uint8_t>(*p);
      if (c == '"' || c == '\\') {
        put('\\');
        put(c);
      } else if (c < 0x20) {
        char esc[7];
        snprintf(esc, sizeof(esc), "\\u%04x", c);
        append(esc);
      } else {
        put(c);
      }
    }
    put('"');
    end(mark);
  }
  void add(const char* key, int value) {
    size_t mark = begin(key);
    char num[12];
    snprintf(num, sizeof(num), "%d", value);
    append(num);
    end(mark);
  }
  void add(const char* key, bool value) {
    size_t mark = begin(key);
    append(value ? "true" : "false");
    end(mark);
  }
  // Close the object; the buffer is NUL-terminated. Returns the length.
  size_t finish() {
    buf[len++] = '}';
    buf[len] = '\0';
    return len;
  }
private:
  size_t begin(const char* key) {
    size_t mark = len;
    ok = true;
    if (fields > 0) put(',');
    put('"');
    append(key);
    put('"');
    put(':');
    return mark;
  }
  void end(size_t mark) {
    if (ok) {
      fields++;
    } else {
      len = mark;
    }
  }
  // Always keep room for the closing brace and the terminator
  void put(char c) {
    if (len + 2 < cap) {
      buf[len++] = c;
    } else {
      ok = false;
    }
  }
  void append(const char* text) {
    while (*text) put(*text++);
  }
  char* buf;
  size_t cap;
  size_t len = 0;
  int fields = 0;
  bool ok = true;
};

/**
 * @brief Generate the JSON status message
 * Writes the current player status into a caller-provided buffer, without
 * building a JsonDocument or String on the heap
 * @param buf Output buffer (STATUS_JSON_SIZE bytes; a field that does not fit is dropped)
 * @param size Size of the output buffer
 * @param fullStatus If true, generates full status; if false, only the bitrate
 * @return Length of the JSON text
 */
size_t generateStatusJSON(char* buf, size_t size, bool fullStatus) {
  JsonObjectWriter json(buf, size);
  if (fullStatus) {
    // Snapshot stream info under the spinlock so core 0 callbacks can't
    // modify the strings while they are being serialized
    StreamInfoData info;
    player.getStreamInfoSnapshot(info);
    // Numbers first and long URLs last: if a field ever overflows the
    // buffer it is dropped, and that should be a URL, not the state
    json.add("playing", player.isPlaying());
    json.add("bitrate", info.bitrate);
    json.add("volume", player.getVolume());
    json.add("bass", player.getBass());
    json.add("mid", player.getMid());
    json.add("treble", player.getTreble());
    json.add("streamName", info.name);
    json.add("streamTitle", info.title);
    json.add("streamURL", info.url);
    json.add("streamIconURL", info.iconUrl);
  } else {
    // Only include the bitrate in partial status
    json.add("bitrate", player.getBitrate());
  }
  return json.finish();
}

/**
 * @brief Send status to all connected WebSocket clients
 * This function broadcasts the current player status to all connected WebSocket clients.
 * The status includes playback state, stream information, bitrate, and volume.
 * It only sends the status if it has changed from the previous status.
 */
void sendStatusToClients(bool fullStatus) {
  // Only broadcast if WebSocket server has clients AND they are connected
  if (webSocket.connectedClients() > 0) {
    char status[STATUS_JSON_SIZE];
    size_t len = generateStatusJSON(status, sizeof(status), fullStatus);
    // Detect changes with a 32-bit FNV-1a hash instead of keeping the last
    // message in RAM. Full and partial formats are tracked separately:
    // comparing a partial {"bitrate":N} frame against the last full status
    // (or vice versa) always looks "changed" and causes redundant broadcasts
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
      hash = (hash ^ static_cast<uint8_t>(status[i])) * 16777619u;
    }
    static uint32_t previousFullHash = 0;
    static uint32_t previousPartialHash = 0;
    uint32_t& previous = fullStatus ? previousFullHash : previousPartialHash;
    // Only send if status has changed
    if (hash != previous) {
      webSocket.broadcastTXT(status, len);
      previous = hash;
    }
  }
}


/**
 * @brief Handle HTTP proxy requests
 * Fetches a plain HTTP URL for the web UI (remote playlists, cover art the
 * browser could not load directly) and streams the response back.
 * @details GET only, and HTTPS is refused: a TLS session needs 30-40 KB of
 * heap, which the radio cannot spare while streaming. The web UI downgrades
 * https:// URLs before calling the proxy.
 */
void handleProxyRequest() {
  // Check if we have a URL parameter
  if (!server.hasArg("url")) {
    sendJsonResponse("error", "Missing URL parameter", 400);
    return;
  }
  // Get the target URL
  String targetUrl = server.arg("url");
  // Validate URL format
  if (!targetUrl.startsWith("http://")) {
    sendJsonResponse("error", "Invalid URL: only http:// is supported", 400);
    return;
  }
  // Create HTTP client
  HTTPClient http;
  // Set timeouts to prevent hanging
  http.setTimeout(5000);
  http.begin(targetUrl);
  // HTTPClient only stores response headers explicitly requested via
  // collectHeaders(); register Content-Type so it can be forwarded
  const char* collectedHeaders[] = {"Content-Type"};
  http.collectHeaders(collectedHeaders, 1);
  int httpResponseCode = http.GET();
  if (httpResponseCode <= 0) {
    http.end();
    Serial.printf("HTTP request failed: %s\n", http.errorToString(httpResponseCode).c_str());
    sendJsonResponse("error", "Proxy request failed: " + String(http.errorToString(httpResponseCode)), 500);
    return;
  }
  // Stream the response directly to the client
  WiFiClient * stream = http.getStreamPtr();
  String contentType = http.header("Content-Type");
  if (contentType.isEmpty()) {
    // Try to determine content type from URL
    String lowerUrl = targetUrl;
    lowerUrl.toLowerCase();
    if (lowerUrl.endsWith(".png")) {
      contentType = "image/png";
    } else if (lowerUrl.endsWith(".jpg") || lowerUrl.endsWith(".jpeg")) {
      contentType = "image/jpeg";
    } else if (lowerUrl.endsWith(".gif")) {
      contentType = "image/gif";
    } else {
      contentType = "application/octet-stream";
    }
  }
  // getSize() returns -1 for chunked/unknown length; cap such responses so an
  // endless stream (e.g. a radio URL) can never block the main loop forever
  const size_t maxProxyBytes = 256 * 1024;
  const unsigned long maxProxyMillis = 10000;
  int sizeHint = http.getSize();
  size_t contentLength = (sizeHint > 0) ? (size_t)sizeHint : 0;
  // Send response with proper content type and length
  server.setContentLength((sizeHint > 0) ? (size_t)sizeHint : CONTENT_LENGTH_UNKNOWN);
  server.send(httpResponseCode, contentType, "");
  // Stream the content
  const size_t bufferSize = 1024;
  uint8_t buffer[bufferSize];
  size_t totalBytesRead = 0;
  unsigned long proxyStart = millis();
  // Read and send data in chunks
  while (http.connected() && server.client().connected() &&
         (contentLength == 0 || totalBytesRead < contentLength)) {
    // Enforce byte and time caps for unknown-length responses
    if (totalBytesRead >= maxProxyBytes || millis() - proxyStart > maxProxyMillis) {
      break;
    }
    size_t bytesAvailable = stream->available();
    if (bytesAvailable) {
      size_t bytesRead = stream->readBytes(buffer, min(bytesAvailable, bufferSize));
      server.client().write(buffer, bytesRead);
      totalBytesRead += bytesRead;
    }
    // Yield to other tasks
    yield();
  }
  // End the HTTP connection
  http.end();
}

/**
 * @brief Handle WebSocket events
 * Processes WebSocket connection, disconnection, and message events
 * @param num Client number
 * @param type Event type (connected, disconnected, text message, etc.)
 * @param payload Message payload for text messages
 * @param length Length of the payload
 */
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_CONNECTED:
      Serial.printf("WebSocket client #%u connected from %d.%d.%d.%d\n", num,
                    webSocket.remoteIP(num)[0], webSocket.remoteIP(num)[1],
                    webSocket.remoteIP(num)[2], webSocket.remoteIP(num)[3]);
      {
        // The handshake is complete when this event fires; send the full
        // status immediately so the new client has the current state
        char status[STATUS_JSON_SIZE];
        size_t len = generateStatusJSON(status, sizeof(status), true);
        // Send status to newly connected client with error checking
        if (webSocket.clientIsConnected(num)) {
            webSocket.sendTXT(num, status, len);
        }
      }
      break;
    case WStype_DISCONNECTED:
      Serial.printf("WebSocket client #%u disconnected\n", num);
      break;
    case WStype_TEXT:
      Serial.printf("WebSocket client #%u text: %s\n", num, payload);
      break;
    default:
      break;
  }
}


/**
 * @brief Update the OLED display with current status
 * Shows playback status, current stream, volume level, and playlist selection
 * This function updates the OLED display with the current player status. When playing,
 * it shows the station name, stream title, bitrate, and volume. When stopped, it shows
 * the selected playlist item and volume. It also implements scrolling text for long strings.
 */
void updateDisplay() {
  // Check if display is initialized
  if (display == nullptr) return;
  // Get current IP address or "No IP" if not connected
  String ipString;
  if (WiFi.status() == WL_CONNECTED) {
    ipString = WiFi.localIP().toString();
  } else {
    ipString = "No IP";
  }
  // Snapshot stream info under the spinlock so core 0 callbacks can't
  // modify the strings while the display renders them
  StreamInfoData info;
  player.getStreamInfoSnapshot(info);
  // When not playing, show the selected playlist item name instead of empty stream name
  const char* displayStreamName = info.name;
  if (!player.isPlaying() && strlen(displayStreamName) == 0) {
    // If we have a playlist and a valid index, show the selected item name
    if (player.isPlaylistIndexValid()) {
      displayStreamName = player.getPlaylistItem(player.getPlaylistIndex()).name;
    }
  }
  // Update the display with current status
  display->update(player.isPlaying(), info.title, displayStreamName, player.getVolume(), info.bitrate, ipString);
}


/**
 * @brief Initialize SPIFFS with error recovery
 * Mounts SPIFFS filesystem with error recovery mechanisms
 * @return true if successful, false otherwise
 */
bool initSPIFFS() {
  // Initialize SPIFFS with error recovery
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    // Try to reformat SPIFFS
    if (!SPIFFS.format()) {
      Serial.println("ERROR: Failed to format SPIFFS");
      return false;
    }
    // Try to mount again after formatting
    if (!SPIFFS.begin(true)) {
      Serial.println("ERROR: Failed to mount SPIFFS after formatting");
      return false;
    }
    Serial.println("SPIFFS formatted and mounted successfully");
  } else {
    Serial.println("SPIFFS mounted successfully");
  }
  // Test SPIFFS write capability on every boot so a corrupted FS is caught early
  Serial.println("Testing SPIFFS write capability...");
  File testFile = SPIFFS.open("/spiffs_test", "w");
  if (!testFile) {
    Serial.println("ERROR: Failed to create SPIFFS test file!");
  } else {
    bool writeOk = testFile.println("SPIFFS write test - OK");
    testFile.close();
    if (writeOk) {
      Serial.println("SPIFFS write test successful");
      SPIFFS.remove("/spiffs_test");
    } else {
      Serial.println("ERROR: Failed to write to SPIFFS test file!");
      SPIFFS.remove("/spiffs_test");
    }
  }
  return true;
}


/**
 * @brief Setup web server routes and static file serving
 * Configures all HTTP routes and static file mappings for the web server
 */
void setupWebServer() {
  // handlePostStreamsUpload() needs the request Content-Type
  const char* requestHeaders[] = {"Content-Type"};
  server.collectHeaders(requestHeaders, 1);
  server.on("/api/streams", HTTP_GET, handleGetStreams);
  server.on("/api/streams", HTTP_POST, handlePostStreams, handlePostStreamsUpload);
  server.on("/api/player", HTTP_GET, handlePlayer);
  server.on("/api/player", HTTP_POST, handlePlayer);
  server.on("/api/mixer", HTTP_GET, handleMixer);
  server.on("/api/mixer", HTTP_POST, handleMixer);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  // /api/config/export deliberately removed: it returned wifi.json with
  // cleartext passwords to any unauthenticated caller
  server.on("/api/config/import", HTTP_POST, handleImportConfig);
  server.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);
  server.on("/api/wifi/save", HTTP_POST, handleWiFiSave);
  server.on("/api/wifi/status", HTTP_GET, handleWiFiStatus);
  server.on("/api/wifi/config", HTTP_GET, handleWiFiConfig);
  server.on("/api/proxy", HTTP_GET, handleProxyRequest);
  server.on("/w", HTTP_GET, handleSimpleWebPage);
  server.on("/w", HTTP_POST, handleSimpleWebPage);
  server.serveStatic("/", SPIFFS, "/player.html");
  server.serveStatic("/playlist", SPIFFS, "/playlist.html");
  server.serveStatic("/wifi", SPIFFS, "/wifi.html");
  server.serveStatic("/config", SPIFFS, "/config.html");
  server.serveStatic("/about", SPIFFS, "/about.html");
  server.serveStatic("/styles.css", SPIFFS, "/styles.css");
  server.serveStatic("/scripts.js", SPIFFS, "/scripts.js");
  server.serveStatic("/pico.min.css", SPIFFS, "/pico.min.css");
  server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico");
  server.serveStatic("/logo.png", SPIFFS, "/logo.png");
}


// Tracks whether the soft-AP is currently running. The AP is an open
// (passwordless) network, so it exposes the whole control surface to anyone
// in range; keep it up only as a fallback when STA is not connected.
static bool apActive = false;

/**
 * @brief Bring the soft-AP up or down to match STA connection state
 * @param wantAP true to ensure the AP is running, false to shut it down
 */
void manageAccessPoint(bool wantAP) {
  if (wantAP && !apActive) {
    Serial.println("Starting Access Point mode...");
    if (WiFi.softAP("CubeRadio")) {
      apActive = true;
      Serial.print("AP IP Address: ");
      Serial.println(WiFi.softAPIP().toString());
      display->showStatus("AP Mode Active", "CubeRadio", WiFi.softAPIP().toString());
    } else {
      Serial.println("Failed to start Access Point");
    }
  } else if (!wantAP && apActive) {
    Serial.println("STA connected, shutting down Access Point");
    WiFi.softAPdisconnect(true);
    apActive = false;
  }
}

/**
 * @brief Connect to WiFi networks
 * Handles connection to configured WiFi networks with scanning and fallback to AP mode
 * @return true if connected to a network, false otherwise
 */
bool connectToWiFi() {
  // Track if this is the first connection attempt
  static bool firstConnection = true;  
  bool connected = false;
  if (wifiNetworkCount > 0) {
    WiFi.setHostname("CubeRadio");
    // First, scan for available networks
    Serial.println("Scanning for available WiFi networks...");
    // Show appropriate status based on whether this is first connection or reconnection
    if (firstConnection) {
      display->showLogo();
    } else {
      display->showStatus("WiFi scanning", "", "");
    }
    int n = WiFi.scanNetworks();
    Serial.printf("Found %d networks\n", n);
    // Create array to track which configured networks are available
    bool networkAvailable[MAX_WIFI_NETWORKS] = {false};
    // Check which configured networks are available
    for (int i = 0; i < wifiNetworkCount; i++) {
      if (strlen(ssid[i]) > 0) {
        for (int j = 0; j < n; j++) {
          if (strcmp(WiFi.SSID(j).c_str(), ssid[i]) == 0) {
            networkAvailable[i] = true;
            Serial.printf("Network %s is available\n", ssid[i]);
            break;
          }
        }
        if (!networkAvailable[i]) {
          Serial.printf("Network %s is not available\n", ssid[i]);
        }
      }
    }
    // Try to connect to available configured networks in background
    for (int i = 0; i < wifiNetworkCount; i++) {
      if (strlen(ssid[i]) > 0 && networkAvailable[i]) {
        display->turnOn();
        Serial.printf("Attempting to connect to %s...\n", ssid[i]);
        display->showStatus("WiFi connecting", "", ssid[i]);
        WiFi.begin(ssid[i], password[i]);
        int wifiAttempts = 0;
        const int maxAttempts = 15;
        while (WiFi.status() != WL_CONNECTED && wifiAttempts < maxAttempts) {
          delay(500);
          Serial.print(".");
          wifiAttempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
          Serial.printf("Connected to %s\n", ssid[i]);
          connected = true;
          // Update display with connection info
          Serial.print("IP Address: ");
          Serial.println(WiFi.localIP().toString());
          display->showStatus(String(WiFi.SSID()), "", WiFi.localIP().toString());
          break;
        } else {
          Serial.printf("Failed to connect to %s\n", ssid[i]);
          // Reset WiFi before trying next network
          WiFi.disconnect();
          delay(1000);
        }
      }
    }
  }
  // If not connected to any network, AP mode is already active
  if (connected) {
    // Mark that first connection has been completed
    firstConnection = false;  
  } else {
    // If we reach here, no networks were connected
    if (wifiNetworkCount > 0) {
      Serial.println("Failed to connect to any configured WiFi network");
    } else {
      Serial.println("No WiFi networks configured");
    }
  }
  // Return connection status
  return connected;
}


/**
 * @brief Arduino main loop function
 * Handles web server requests, WebSocket events, rotary encoder input, and MPD commands
 * This is the main application loop that runs continuously after setup()
 */
void loop() {
  // Drain updates requested by audio callbacks running on core 0.
  // updateDisplay() and sendStatusToClients() are not multicore-safe, so
  // callbacks only set this flag and the main loop does the actual work.
  if (pendingCallbackUpdate) {
    pendingCallbackUpdate = false;
    updateDisplay();
    sendStatusToClients();
  }

  server.handleClient();         // Process incoming web requests
  webSocket.loop();              // Process WebSocket events
  mpdInterface.handleClient();   // Process MPD commands
  handleBoardButton();           // Process board button input
  handleRotary();                // Process rotary encoder input
  handleTouch();                 // Process touch button actions

  // Periodically update display for scrolling text animation
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 500) {  // Update every 500ms for smooth scrolling
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  // Check audio connection status with improved error recovery
  static unsigned long streamStoppedTime = 0;
  if (player.getAudioObject()) {
    // Check if audio is still connected
    if (player.isPlaying()) {
      // Give a freshly started stream 5 s of grace: isRunning() is false while
      // the library follows HTTP redirects, and restarting then caused
      // duplicate connects on every station start.
      bool inStartupGrace = millis() - player.getPlayStartTime() < 5000;
      if (!player.isRunning() && !inStartupGrace) {
        Serial.println("Audio stream stopped unexpectedly");
        // Attempt to restart the stream if it was playing
        if (strlen(player.getStreamUrl()) > 0) {
          // Wait 1 second before attempting to restart (non-blocking)
          if (streamStoppedTime == 0) {
            // First time detecting the stream has stopped
            streamStoppedTime = millis();
            Serial.println("Waiting 1 second before restart attempt...");
          } else if (millis() - streamStoppedTime >= 1000) {
            // 1 second has passed, attempt to restart
            Serial.println("Attempting to restart stream...");
            // Resume the current stream
            player.startStream();
            // Reset the timer
            streamStoppedTime = 0;
          }
        }
      } else {
        // Stream is running
        streamStoppedTime = 0;
        // Update the bitrate if it has changed
        player.updateBitrate();
      }

      // Send status to clients every 3 seconds instead of 2 to reduce load
      static unsigned long lastStatusUpdate = 0;
      if (millis() - lastStatusUpdate > 3000) {  // Changed from 2000 to 3000
        // Only send if there are connected clients
        if (webSocket.connectedClients() > 0) {
          // Send partial status update
          sendStatusToClients(false);
        }
        // Update the timestamp
        lastStatusUpdate = millis();
      }
    }
  }
  
  // Periodic cleanup with error recovery - add network status check
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 60000) {  // Every 60 seconds instead of 30
    lastCleanup = millis();
    // Check WiFi status and reconnect if needed
    if (wifiNetworkCount > 0 && WiFi.status() != WL_CONNECTED) {
      connectToWiFi();
    }
    // Keep the fallback AP up only while STA is down
    manageAccessPoint(WiFi.status() != WL_CONNECTED);
  }

  // Hourly heap telemetry: makes slow fragmentation/leaks visible in the
  // serial log over long uptimes (largest block tracks fragmentation)
  static unsigned long lastHeapLog = 0;
  if (millis() - lastHeapLog > 3600000UL) {
    lastHeapLog = millis();
    Serial.printf("Heap: free %u, min free %u, largest block %u\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
  }
  
  // Handle display timeout with configurable timeout value
  display->handleTimeout(player.isPlaying(), millis());

  // Small delay to prevent busy waiting and reduce network load
  delay(150);  // Increased from 100 to 150 to reduce CPU usage
}


/**
 * @brief Arduino setup function
 * Initializes all system components including WiFi, audio, display, and servers
 * This function is called once at startup to configure the hardware and software components.
 */
void setup() {
  Serial.begin(115200);
  // Print program name and build timestamp
  Serial.println("CubeRadio - An ESP32-based internet radio player with MPD protocol support");
  Serial.print("Build timestamp: ");
  Serial.println(BUILD_TIME);
  
  // Initialize PSRAM if available
  #if defined(BOARD_HAS_PSRAM)
  if (psramInit()) {
    Serial.println("PSRAM initialized successfully");
    Serial.printf("PSRAM size: %d bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("PSRAM initialization failed");
  }
  #endif

  // Initialize SPIFFS with error recovery
  if (!initSPIFFS()) {
    Serial.println("ERROR: Failed to initialize SPIFFS");
    return;
  }
  // Load configuration
  loadConfig();
  
  // Validate display type; if config was just created from defaults, save it now
  // so the file reflects the validated state.
  bool configWasDefault = !SPIFFS.exists("/config.json");
  if (config.display_type < 0 || config.display_type >= getDisplayTypeCount()) {
    config.display_type = 0;
  }
  if (configWasDefault) {
    saveConfig();
  }
  // Initialize LED pin if configured
  if (config.led_pin >= 0) {
    pinMode(config.led_pin, OUTPUT);
    digitalWrite(config.led_pin, LOW);  // Turn off LED initially
  }
  // Initialize board button with pull-up resistor if configured and different from rotary switch
  if (config.board_button >= 0 && config.board_button != config.rotary_sw) {
    pinMode(config.board_button, INPUT_PULLUP);
    // Attach interrupt handler for board button press
    attachInterrupt(digitalPinToInterrupt(config.board_button), boardButtonISR, FALLING);
  }
  // Initialize OLED display
  // Configure I2C pins
  Wire.begin(config.display_sda, config.display_scl);
  // Get display dimensions based on display type
  int displayWidth, displayHeight;
  if (!getDisplaySize(config.display_type, &displayWidth, &displayHeight)) {
    // Fallback to default if invalid display type
    displayWidth = 128;
    displayHeight = 64;
  }
  // Create display objects after config is loaded
  displayOLED = new Adafruit_SSD1306(displayWidth, displayHeight, &Wire, -1);
  display = new Display(*displayOLED, (enum display_t)config.display_type);
  display->begin();
  
#ifndef DISABLE_TOUCH
  // Initialize touch buttons
  if (config.touch_play >= 0) {
    touchPlay = new TouchButton(config.touch_play, config.touch_threshold, config.touch_debounce, true);
  }
  if (config.touch_next >= 0) {
    touchNext = new TouchButton(config.touch_next, config.touch_threshold, config.touch_debounce, true);
  }
  if (config.touch_prev >= 0) {
    touchPrev = new TouchButton(config.touch_prev, config.touch_threshold, config.touch_debounce, true);
  }
  // Log baseline (untouched) readings to help calibrate touch_threshold:
  // arduino-esp32 3.x touch values RISE on touch, unlike 2.x where they fell
  if (touchPlay) Serial.printf("Touch play (GPIO %d) baseline: %u, threshold: %d\n",
                               config.touch_play, touchPlay->getTouchValue(), config.touch_threshold);
  if (touchNext) Serial.printf("Touch next (GPIO %d) baseline: %u, threshold: %d\n",
                               config.touch_next, touchNext->getTouchValue(), config.touch_threshold);
  if (touchPrev) Serial.printf("Touch prev (GPIO %d) baseline: %u, threshold: %d\n",
                               config.touch_prev, touchPrev->getTouchValue(), config.touch_threshold);
#else
  Serial.println("Touch interface disabled at compile time (DISABLE_TOUCH)");
#endif

  // Load WiFi credentials with error recovery
  loadWiFiCredentials();
  // Connect to WiFi with error handling
  bool staConnected = connectToWiFi();
  // Start the open AP only as a fallback when not connected to home WiFi;
  // it is brought up/down dynamically as STA state changes (see loop())
  manageAccessPoint(!staConnected);

  // Start mDNS responder (CubeRadio.local)
  if (MDNS.begin("CubeRadio")) {
    Serial.println("MDNS responder started");
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("mpd", "tcp", 6600);
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
  
  // Setup audio output with error handling
  player.setupAudioOutput();
  // Setup rotary encoder with error handling
  setupRotaryEncoder();
  // Load playlist with error recovery
  player.loadPlaylist();
  // Start audio task before loadPlayerState() so that connecttohost() called
  // during stream resume has audio->loop() running to drain the socket.
  BaseType_t result = xTaskCreatePinnedToCore(audioTask, "AudioTask", 8192, NULL, 5, &audioTaskHandle, 0);
  if (result != pdPASS) {
    Serial.println("ERROR: Failed to create AudioTask");
  } else {
    Serial.println("AudioTask created successfully");
  }
  // Load player state (may call startStream if previously playing)
  player.loadPlayerState();
  if (player.isPlaying()) {
    // Update activity time to prevent display from timing out immediately
    display->setActivityTime(millis()); 
  }
  // Setup web server routes
  setupWebServer();
   // Start server
  server.begin();
  Serial.println("Web server started");
    // Setup WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
    // Start MPD server
  mpdServer.begin();
  Serial.println("MPD server started");
  
  // Update display
  updateDisplay();
}
