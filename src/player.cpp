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

#include "player.h"
#include "playlist.h"
#include <WiFi.h>
#include "main.h"
#include <ArduinoJson.h>
#include <Audio.h>

/**
 * @brief Player constructor
 */
Player::Player() {
  audio = nullptr;
  playlist = new Playlist();
  // Initialize player state with defaults
  clearPlayerState();
  // Initialize stream info
  clearStreamInfo();
}

/**
 * @brief Set playlist index with validation
 * @param index New playlist index
 */
void Player::setPlaylistIndex(int index) {
  // If playlist is empty, index must be -1
  if (playlist->getCount() <= 0) {
    playerState.playlistIndex = -1;
  }
  // If playlist has items, validate that the index is within the valid range
  else if (index >= 0 && index < playlist->getCount()) {
    playerState.playlistIndex = index;
  } else {
    // If index is out of bounds, set to 0 (first item)
    playerState.playlistIndex = 0;
  }
}

/**
 * @brief Set player volume
 * @param volume New volume level (0-22)
 */
void Player::setVolume(int volume) {
  // Validate and set volume
  playerState.volume = constrain(volume, 0, 22);
  // Mark state as dirty when volume changes
  setDirty();
  // Apply volume to audio output
  if (audio) {
    audio->setVolume(playerState.volume);
  }
}

/**
 * @brief Set tone controls (bass, mid, treble)
 * Applies the tone settings to the audio output
 */
void Player::setTone() {
  // Apply tone settings to audio output
  if (audio) {
    audio->setTone(playerState.bass, playerState.mid, playerState.treble);
  }
}

/**
 * @brief Set tone controls (bass, mid, treble)
 * Applies the tone settings to the audio output
 * @param bass Bass level (-6 to 6)
 * @param mid Mid level (-6 to 6)
 * @param treble Treble level (-6 to 6)
 */
void Player::setTone(int bass, int mid, int treble) {
  // Validate and set tone values
  playerState.bass = constrain(bass, -6, 6);
  playerState.mid = constrain(mid, -6, 6);
  playerState.treble = constrain(treble, -6, 6);
  // Mark state as dirty when tone changes
  setDirty();
  // Apply tone settings to audio output
  setTone();
}

/**
 * @brief Set stream URL
 * @param url New stream URL
 */
void Player::setStreamUrl(const char* url) {
  taskENTER_CRITICAL(&spinlock);
  if (url) {
    strncpy(streamInfo.url, url, sizeof(streamInfo.url) - 1);
    streamInfo.url[sizeof(streamInfo.url) - 1] = '\0';
  } else {
    streamInfo.url[0] = '\0';
  }
  taskEXIT_CRITICAL(&spinlock);
}

/**
 * @brief Set stream name
 * @param name New stream name
 */
void Player::setStreamName(const char* name) {
  taskENTER_CRITICAL(&spinlock);
  if (name) {
    strncpy(streamInfo.name, name, sizeof(streamInfo.name) - 1);
    streamInfo.name[sizeof(streamInfo.name) - 1] = '\0';
  } else {
    streamInfo.name[0] = '\0';
  }
  taskEXIT_CRITICAL(&spinlock);
}

/**
 * @brief Set stream title
 * @param title New stream title
 */
void Player::setStreamTitle(const char* title) {
  taskENTER_CRITICAL(&spinlock);
  if (title) {
    strncpy(streamInfo.title, title, sizeof(streamInfo.title) - 1);
    streamInfo.title[sizeof(streamInfo.title) - 1] = '\0';
  } else {
    streamInfo.title[0] = '\0';
  }
  taskEXIT_CRITICAL(&spinlock);
}

/**
 * @brief Set stream ICY URL
 * @param icyUrl New stream ICY URL
 */
void Player::setStreamIcyUrl(const char* icyUrl) {
  taskENTER_CRITICAL(&spinlock);
  if (icyUrl) {
    strncpy(streamInfo.icyUrl, icyUrl, sizeof(streamInfo.icyUrl) - 1);
    streamInfo.icyUrl[sizeof(streamInfo.icyUrl) - 1] = '\0';
  } else {
    streamInfo.icyUrl[0] = '\0';
  }
  taskEXIT_CRITICAL(&spinlock);
}

/**
 * @brief Set stream icon URL
 * @param iconUrl New stream icon URL
 */
void Player::setStreamIconUrl(const char* iconUrl) {
  taskENTER_CRITICAL(&spinlock);
  if (iconUrl) {
    strncpy(streamInfo.iconUrl, iconUrl, sizeof(streamInfo.iconUrl) - 1);
    streamInfo.iconUrl[sizeof(streamInfo.iconUrl) - 1] = '\0';
  } else {
    streamInfo.iconUrl[0] = '\0';
  }
  taskEXIT_CRITICAL(&spinlock);
}

/**
 * @brief Clear all stream information
 */
void Player::clearStreamInfo() {
  streamInfo.url[0] = '\0';
  streamInfo.name[0] = '\0';
  streamInfo.title[0] = '\0';
  streamInfo.icyUrl[0] = '\0';
  streamInfo.iconUrl[0] = '\0';
  streamInfo.bitrate = 0;
}

/**
 * @brief Clear player state to default values
 * Resets all player state variables to their default values
 */
void Player::clearPlayerState() {
  playerState.playing = false;
  playerState.volume = 8;
  playerState.bass = 0;
  playerState.mid = 0;
  playerState.treble = 0;
  playerState.playlistIndex = -1;
  playerState.lastSaveTime = 0;
  playerState.dirty = false;
  playerState.playStartTime = 0;
  playerState.totalPlayTime = 0;
}

/**
 * @brief Load player state from SPIFFS
 */
void Player::loadPlayerState() {
  DynamicJsonDocument doc(PLAYER_STATE_BUFFER_SIZE);  // Use predefined buffer size
  if (readJsonFile("/player.json", PLAYER_STATE_BUFFER_SIZE, doc)) {
    playerState.playing = doc["playing"] | false;
    playerState.volume = doc["volume"] | 8;
    playerState.bass = doc["bass"] | 0;
    playerState.mid = doc["mid"] | 0;
    playerState.treble = doc["treble"] | 0;
    setPlaylistIndex(doc["playlistIndex"] | -1);
    playerState.totalPlayTime = doc["totalPlayTime"] | 0UL;
    Serial.println("Loaded player state from SPIFFS");
  } else {
    Serial.println("No player state file found, using defaults");
    clearPlayerState();
  }
  // Apply loaded state
  if (audio) {
    audio->setVolume(playerState.volume);
    audio->setTone(playerState.bass, playerState.mid, playerState.treble);
  }
  // If it was playing, resume playback — but only if WiFi is already connected.
  // Attempting to stream without a network silently fails and leaves playing=true
  // with no audio, confusing the watchdog restart logic.
  if (playerState.playing && isPlaylistIndexValid()) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Resuming playback from saved state");
      startStream(getCurrentPlaylistItemURL(), getCurrentPlaylistItemName());
    } else {
      Serial.println("WiFi not connected, deferring playback resume");
      playerState.playing = false;
    }
  }
}

/**
 * @brief Save player state to SPIFFS
 */
void Player::savePlayerState() {
  DynamicJsonDocument doc(PLAYER_STATE_BUFFER_SIZE);  // Use predefined buffer size
  doc["playing"] = playerState.playing;
  doc["volume"] = playerState.volume;
  doc["bass"] = playerState.bass;
  doc["mid"] = playerState.mid;
  doc["treble"] = playerState.treble;
  doc["playlistIndex"] = playerState.playlistIndex;
  doc["totalPlayTime"] = playerState.totalPlayTime;
  if (writeJsonFile("/player.json", doc)) {
    Serial.println("Saved player state to SPIFFS");
    // Use critical section to protect against concurrent access to dirty flag
    resetDirty();
  } else {
    Serial.println("Failed to save player state to SPIFFS");
  }
}

/**
 * @brief Load playlist from SPIFFS storage
 * Delegates to the playlist object's load method
 */
void Player::loadPlaylist() {
  playlist->load();  // Load using predefined buffer size PLAYLIST_BUFFER_SIZE
  playlistVersion++;
}

/**
 * @brief Save playlist to SPIFFS storage
 * Delegates to the playlist object's save method
 */
void Player::savePlaylist() {
  playlist->save();  // Save using predefined buffer size PLAYLIST_BUFFER_SIZE
}

/**
 * @brief Set playlist item at specific index
 * @param index Playlist index
 * @param name Stream name
 * @param url Stream URL
 * Delegates to the playlist object's setItem method
 */
void Player::setPlaylistItem(int index, const char* name, const char* url) {
  playlist->setItem(index, name, url);
  playlistVersion++;
}

/**
 * @brief Add playlist item
 * @param name Stream name
 * @param url Stream URL
 * Delegates to the playlist object's addItem method
 */
bool Player::addPlaylistItem(const char* name, const char* url) {
  bool added = playlist->addItem(name, url);
  if (added) playlistVersion++;
  return added;
}

/**
 * @brief Remove playlist item at specific index
 * @param index Playlist index to remove
 * Delegates to the playlist object's removeItem method
 */
void Player::removePlaylistItem(int index) {
  playlist->removeItem(index);
  playlistVersion++;
  // Keep playlistIndex pointing at the same logical stream after removal.
  // If the removed entry was before the current index, shift down by one.
  // If it was the current entry (or the list is now empty), clamp to 0.
  if (playlist->getCount() == 0) {
    playerState.playlistIndex = -1;
  } else if (index < playerState.playlistIndex) {
    playerState.playlistIndex--;
  } else if (playerState.playlistIndex >= playlist->getCount()) {
    playerState.playlistIndex = playlist->getCount() - 1;
  }
}

/**
 * @brief Clear all playlist items
 * Delegates to the playlist object's clear method
 */
void Player::clearPlaylist() {
  playlist->clear();
  playlistVersion++;
}

/**
 * @brief Get the number of items in the playlist
 * @return int Number of items in the playlist
 * Delegates to the playlist object's getCount method
 */
int Player::getPlaylistCount() const {
  return playlist->getCount();
}

/**
 * @brief Get the next playlist item index with wraparound
 * @details Calculates the next playlist index with wraparound behavior.
 * If the playlist is empty, returns 0. Otherwise, returns the next index
 * in the playlist, wrapping to 0 when reaching the end.
 * @return Next playlist item index
 */
int Player::getNextPlaylistItem() const {
  if (playlist->getCount() <= 0) {
    // No items
    return -1;
  }
  return (playerState.playlistIndex + 1) % playlist->getCount();
}

/**
 * @brief Get the previous playlist item index
 * @details Calculates the previous playlist index.
 * If the playlist is empty or index is at zero or below, returns 0.
 * Otherwise, returns the previous index in the playlist.
 * @return Previous playlist item index
 */
int Player::getPrevPlaylistItem() const {
  int n = playlist->getCount();
  if (n <= 0) return -1;
  if (playerState.playlistIndex <= 0) return n - 1;  // wrap to last
  return playerState.playlistIndex - 1;
}

/**
 * @brief Check if the current playlist index is valid
 * @details Validates that the playlist has items and the current index
 * is within the valid range of the playlist.
 * @return true if playlist index is valid, false otherwise
 */
bool Player::isPlaylistIndexValid() const {
  return (playlist->getCount() > 0 &&
          playerState.playlistIndex >= 0 &&
          playerState.playlistIndex < playlist->getCount());
}

/**
 * @brief Get the name of the current playlist item
 * @return const char* Name of the current playlist item
 */
const char* Player::getCurrentPlaylistItemName() const {
  if (isPlaylistIndexValid()) {
    return playlist->getItem(playerState.playlistIndex).name;
  }
  return "";
}

/**
 * @brief Get the URL of the current playlist item
 * @return const char* URL of the current playlist item
 */
const char* Player::getCurrentPlaylistItemURL() const {
  if (isPlaylistIndexValid()) {
    return playlist->getItem(playerState.playlistIndex).url;
  }
  return "";
}

/**
 * @brief Get a playlist item at specific index
 * @param index Playlist index
 * @return const StreamInfo& Reference to the playlist item
 * Delegates to the playlist object's getItem method
 */
const StreamInfo& Player::getPlaylistItem(int index) const {
  return playlist->getItem(index);
}

/**
 * @brief Start streaming an audio stream
 * Stops any currently playing stream and begins playing a new one
 * If called without parameters, resumes playback of streamURL if available
 * @param url URL of the audio stream to play (optional)
 * @param name Human-readable name of the stream (optional)
 */
void Player::startStream(const char* url, const char* name) {
  bool resume = false;
  // Stop the currently playing stream if the stream changes
  if (audio && url && strlen(url) > 0) {
    // Stop first
    stopStream();
  }
  // If no URL provided, check if we have a current stream to resume
  if (!url || strlen(url) == 0) {
    if (strlen(streamInfo.url) > 0) {
      // Resume playback of current stream
      url = streamInfo.url;
      // Use current name if available, otherwise use a default
      if (!name || strlen(name) == 0) {
        name = (strlen(streamInfo.name) > 0) ? streamInfo.name : "Unknown Station";
      }
      // We are resuming playback
      resume = true;
    } else {
      Serial.println("Error: No URL provided and no current stream to resume");
      return;
    }
  }
  // Validate URL pointer (name was already defaulted above if needed)
  if (!url) {
    Serial.println("Error: NULL stream URL pointer passed to startStream");
    return;
  }
  // Allow empty name — use fallback
  if (!name || strlen(name) == 0) {
    name = "Unknown Station";
  }
  // Validate URL format
  if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
    Serial.println("Error: Invalid URL format");
    return;
  }
  // Keep the stream url and name if they are new
  if (!resume) {
    strncpy(streamInfo.url, url, sizeof(streamInfo.url) - 1);
    streamInfo.url[sizeof(streamInfo.url) - 1] = '\0';
    strncpy(streamInfo.name, name, sizeof(streamInfo.name) - 1);
    streamInfo.name[sizeof(streamInfo.name) - 1] = '\0';
  }
  // Turn on LED when playing (if LED pin is configured)
  if (config.led_pin >= 0) {
    digitalWrite(config.led_pin, HIGH);
  }
  // Use ESP32-audioI2S to play the stream; only set playing on confirmed connect
  if (audio) {
    bool audioConnected = audio->connecttohost(url);
    if (!audioConnected) {
      Serial.println("Error: Failed to connect to audio stream");
      playerState.playing = false;
      clearStreamInfo();
      if (config.led_pin >= 0) digitalWrite(config.led_pin, LOW);
    } else {
      playerState.playing = true;
      // Track play time (store raw millis so subtraction wraps safely)
      playerState.playStartTime = millis();
      Serial.println("Successfully connected to audio stream");
    }
  } else {
    Serial.println("Error: Audio not initialised, cannot start stream");
    playerState.playing = false;
    if (config.led_pin >= 0) digitalWrite(config.led_pin, LOW);
  }
  updateDisplay();        // Refresh the display with new playback info
  sendStatusToClients();  // Notify clients of status change
}

/**
 * @brief Stop the currently playing stream
 * Cleans up audio components and resets playback state
 * This function stops audio playback, clears stream information, and resets
 * the playback state to stopped.
 */
void Player::stopStream() {
  // Stop the audio playback
  if (audio) {
    audio->stopSong();
  }
  // Set playback status to stopped
  playerState.playing = false;
  clearStreamInfo();
  // Update total play time when stopping
  if (playerState.playStartTime > 0) {
    playerState.totalPlayTime += (millis() - playerState.playStartTime) / 1000;
    playerState.playStartTime = 0;
    setDirty();
  }
  // Turn off LED when stopped (if LED pin is configured)
  if (config.led_pin >= 0) {
    digitalWrite(config.led_pin, LOW);
  }
  updateDisplay();  // Refresh the display
  sendStatusToClients();  // Notify clients of status change
}

/**
 * @brief Initialize audio output interface
 * Configures the selected audio output method
 * This function initializes the ESP32-audioI2S library with I2S pin configuration
 * and sets up the audio buffer with an increased size for better performance.
 * @return Pointer to the initialized Audio object, or nullptr if initialization failed
 */
Audio* Player::setupAudioOutput() {
  // Clean up existing audio object if it exists
  if (audio != nullptr) {
    delete audio;
    audio = nullptr;
  }
  // Initialize ESP32-audioI2S
  audio = new Audio(false); // false = use I2S, true = use DAC
  // Check if allocation succeeded
  if (audio == nullptr) {
    Serial.println("Error: Failed to allocate Audio object");
    return nullptr;
  }
  // Configure I2S pinout from settings
  audio->setPinout(config.i2s_bclk, config.i2s_lrc, config.i2s_dout);
  audio->setVolume(playerState.volume); // Use 0-22 scale directly
  #if defined(BOARD_HAS_PSRAM)
  Serial.println("PSRAM supported, using larger audio buffer");
  audio->setBufsize(4096, 1048576); // 4KB in RAM, 1MB in PSRAM
  #else
  Serial.println("PSRAM not supported on this board, using smaller audio buffer");
  audio->setBufsize(8192, 0); // 32KB in RAM only
  #endif
  return audio;
}

/**
 * @brief Check if audio is currently running
 * @return true if audio is running, false otherwise
 */
bool Player::isRunning() const {
  return audio ? audio->isRunning() : false;
}

/**
 * @brief Handle audio processing loop
 * Processes audio data and maintains playback state
 * This function should be called regularly in the main loop to process
 * audio data and maintain proper playback functionality.
 */
void Player::handleAudio() {
  if (audio) {
    audio->loop();
  }
}

/**
 * @brief Update the current stream bitrate
 * Gets the current bitrate from the audio object and updates the stream info
 * @return The current bitrate in kbps
 */
int Player::updateBitrate() {
  if (audio) {
    int newBitrate = audio->getBitRate() / 1000;  // Convert bps to kbps
    if (newBitrate > 0 && newBitrate != streamInfo.bitrate) {
      setBitrate(newBitrate);
      return newBitrate;
    }
  }
  return streamInfo.bitrate;
}
/**
 * @brief Set the dirty flag to indicate state has changed
 */
void Player::setDirty() {
  playerState.dirty = true;
}

/**
 * @brief Reset the dirty flag
 */
void Player::resetDirty() {
  playerState.dirty = false;
}
