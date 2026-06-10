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

#include "playlist.h"
#include "main.h"
#include <ArduinoJson.h>

extern bool readJsonFile(const char* filename, size_t maxFileSize, DynamicJsonDocument& doc);
extern bool writeJsonFile(const char* filename, DynamicJsonDocument& doc);

/**
 * @brief Playlist constructor
 */
Playlist::Playlist() {
  count = 0;
  for (int i = 0; i < MAX_PLAYLIST_SIZE; i++) {
    playlist[i].name[0] = '\0';
    playlist[i].url[0] = '\0';
  }
}

/**
 * @brief Load playlist from SPIFFS storage
 * Reads playlist.json from SPIFFS and populates the playlist array
 * This function loads the playlist from SPIFFS with error recovery mechanisms.
 * If the playlist file is corrupted, it creates a backup and a new empty playlist.
 */
void Playlist::load() {
  count = 0;
  DynamicJsonDocument doc(PLAYLIST_BUFFER_SIZE);
  if (!readJsonFile("/playlist.json", PLAYLIST_BUFFER_SIZE, doc)) {
    Serial.println("Failed to load playlist, continuing with empty playlist");
    return;
  }
  if (!doc.is<JsonArray>()) {
    Serial.println("Error: Playlist JSON is not an array, continuing with empty playlist");
    return;
  }
  JsonArray array = doc.as<JsonArray>();
  for (JsonObject item : array) {
    if (count >= MAX_PLAYLIST_SIZE) {
      Serial.println("Warning: Playlist limit reached (20 entries)");
      break;
    }
    if (item.containsKey("name") && item.containsKey("url")) {
      const char* name = item["name"];
      const char* url  = item["url"];
      if (name && url && strlen(name) > 0 && strlen(url) > 0) {
        if (VALIDATE_URL(url)) {
          SAFE_STRNCPY(playlist[count].name, name, STREAM_NAME_SIZE);
          SAFE_STRNCPY(playlist[count].url,  url,  STREAM_URL_SIZE);
          count++;
        } else {
          Serial.println("Warning: Skipping stream with invalid URL format");
        }
      } else {
        Serial.println("Warning: Skipping stream with empty name or URL");
      }
    }
  }
  if (count == 0) {
    Serial.println("No valid streams found in playlist");
  } else {
    Serial.printf("Loaded %d streams from playlist\n", count);
  }
  validate();
}

/**
 * @brief Save playlist to SPIFFS storage
 * Serializes the current playlist array to playlist.json
 * This function saves the current playlist to SPIFFS with backup functionality.
 * It creates a backup before saving and restores from backup if saving fails.
 */
void Playlist::save() {
  // Always allocate the maximum allowed buffer; dynamic under-estimation caused
  // silent truncation when entries were near their size limits.
  DynamicJsonDocument doc(PLAYLIST_BUFFER_SIZE);
  JsonArray array = doc.to<JsonArray>();
  // Add playlist entries
  for (int i = 0; i < count; i++) {
    // Validate URL format before saving
    if (strlen(playlist[i].url) == 0 ||
        !VALIDATE_URL(playlist[i].url)) {
      Serial.println("Warning: Skipping stream with invalid URL format during save");
      continue;
    }
    // Create JSON object for the playlist entry
    JsonObject item = array.createNestedObject();
    item["name"] = playlist[i].name;
    item["url"] = playlist[i].url;
  }
  // Save the JSON document to SPIFFS using helper function
  if (writeJsonFile("/playlist.json", doc)) {
    Serial.println("Saved playlist to SPIFFS");
  } else {
    Serial.println("Failed to save playlist to SPIFFS");
  }
}

/**
 * @brief Set playlist item at specific index
 * @param index Playlist index
 * @param name Stream name
 * @param url Stream URL
 */
void Playlist::setItem(int index, const char* name, const char* url) {
  // Only allow indices within the already-populated range or the next append slot.
  // Allowing index > count would create uninitialised sparse slots.
  if (index < 0 || index > count || index >= MAX_PLAYLIST_SIZE || !name || !url) return;
  if (strlen(url) == 0 || !VALIDATE_URL(url)) {
    Serial.println("Warning: Skipping stream with invalid URL format in setItem");
    return;
  }
  // A truncated URL would still look valid but never connect
  if (strlen(url) >= STREAM_URL_SIZE) {
    Serial.println("Warning: Skipping stream with too long URL in setItem");
    return;
  }
  SAFE_STRNCPY(playlist[index].name, name, STREAM_NAME_SIZE);
  SAFE_STRNCPY(playlist[index].url,  url,  STREAM_URL_SIZE);
  if (index == count) {
    count++;
  }
}

/**
 * @brief Add playlist item
 * @param name Stream name
 * @param url Stream URL
 */
bool Playlist::addItem(const char* name, const char* url) {
  if (count >= MAX_PLAYLIST_SIZE) {
    Serial.println("Warning: Playlist full, cannot add item");
    return false;
  }
  if (!name || !url || strlen(url) == 0 || !VALIDATE_URL(url)) {
    Serial.println("Warning: Skipping stream with invalid URL format in addItem");
    return false;
  }
  // A truncated URL would still look valid but never connect
  if (strlen(url) >= STREAM_URL_SIZE) {
    Serial.println("Warning: Skipping stream with too long URL in addItem");
    return false;
  }
  SAFE_STRNCPY(playlist[count].name, name, STREAM_NAME_SIZE);
  SAFE_STRNCPY(playlist[count].url,  url,  STREAM_URL_SIZE);
  count++;
  return true;
}

/**
 * @brief Remove playlist item at specific index
 * @param index Playlist index to remove
 */
void Playlist::removeItem(int index) {
  if (index >= 0 && index < count) {
    // Shift all items after the removed item
    for (int i = index; i < count - 1; i++) {
      SAFE_STRNCPY(playlist[i].name, playlist[i + 1].name, STREAM_NAME_SIZE);
      SAFE_STRNCPY(playlist[i].url, playlist[i + 1].url, STREAM_URL_SIZE);
    }
    // Clear the last item
    playlist[count - 1].name[0] = '\0';
    playlist[count - 1].url[0] = '\0';
    count--;
  }
}

/**
 * @brief Clear all playlist items
 */
void Playlist::clear() {
  for (int i = 0; i < count; i++) {
    playlist[i].name[0] = '\0';
    playlist[i].url[0] = '\0';
  }
  count = 0;
}

/**
 * @brief Get the number of items in the playlist
 * @return Number of items in the playlist
 */
int Playlist::getCount() const {
  return count;
}

/**
 * @brief Get the current playlist index
 * @return Current selected playlist index
 */
/**
 * @brief Get playlist item at specific index
 * @param index Playlist index (0-based)
 * @return Reference to StreamInfo at the specified index, or empty item if out of bounds
 */
const StreamInfo& Playlist::getItem(int index) const {
  if (index < 0 || index >= count) {
    static StreamInfo empty = {"", ""};
    return empty;
  }
  return playlist[index];
}

/**
 * @brief Set the current playlist index
 * @param index New current index (0-based)
 */
/**
 * @brief Validate playlist integrity
 * Ensures playlist count and selection are within valid ranges
 */
void Playlist::validate() {
  if (count < 0 || count > MAX_PLAYLIST_SIZE) {
    Serial.println("Warning: Invalid playlist count detected, resetting to 0");
    count = 0;
  }
}
