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

#ifndef PLAYLIST_H
#define PLAYLIST_H

#include "main.h"
#include <Arduino.h>
#include <FS.h>
#include <functional>

// Constants for StreamInfo field sizes
#define STREAM_NAME_SIZE 96
#define STREAM_URL_SIZE 256   // Must be >= StreamInfoData::url[256] so URLs are never silently truncated

// Playlist storage: one JSON object per line ({"name":"...","url":"..."})
#define PLAYLIST_FILE        "/playlist.jsonl"
#define PLAYLIST_TMP_FILE    "/playlist.tmp"
#define PLAYLIST_BAK_FILE    "/playlist.bak"
#define PLAYLIST_LEGACY_FILE "/playlist.json"   // pre-JSONL format, migrated at boot
#define PLAYLIST_LINE_MAX    1024               // Max bytes per JSONL line, including escapes

// Helper macro for safe string copying with null termination
#define SAFE_STRNCPY(dest, src, size) \
  do { \
    strncpy((dest), (src), (size) - 1); \
    (dest)[(size) - 1] = '\0'; \
  } while (0)

// Structure for playlist items
struct StreamInfo {
  char name[STREAM_NAME_SIZE];
  char url[STREAM_URL_SIZE];
};

/**
 * @brief Playlist kept on SPIFFS, not in RAM
 * @details The playlist lives in PLAYLIST_FILE as JSON Lines. Only the file
 * offset of each entry is kept in memory (4 bytes per entry), plus one cached
 * entry. Entries are read on demand; the whole list is replaced by streaming
 * an upload into a temporary file that is validated line by line and swapped
 * in only when every line is valid.
 */
class Playlist {
private:
  uint32_t offsets[MAX_PLAYLIST_SIZE];  ///< File offset of each valid entry
  int count;                            ///< Number of valid entries
  mutable StreamInfo cache;             ///< Last entry read by getItem()
  mutable int cacheIndex;               ///< Index held in cache, -1 if none

  // Upload state; the line buffer is allocated only while an upload runs
  File uploadFile;
  char* uploadLine;
  size_t uploadLineLen;
  size_t uploadBytes;
  int uploadCount;
  int uploadLineNo;
  bool uploadActive;
  bool uploadOverflow;
  bool uploadLenient;                   ///< Skip invalid lines instead of failing (migration)
  char uploadError[80];

  bool readItem(File& file, int index, StreamInfo& out) const;
  void processUploadLine();
  void failUpload(const char* message);
  void releaseUpload();
  bool migrateLegacy();

public:
  Playlist();

  // Build the offset index from SPIFFS (migrates the legacy JSON file once)
  void load();

  // Getters
  int getCount() const;
  // Returned reference stays valid until the next getItem() for another index
  const StreamInfo& getItem(int index) const;
  int findByUrl(const char* url) const;
  // Visit entries in order with one open file; return false from fn to stop
  void forEach(const std::function<bool(int, const StreamInfo&)>& fn) const;

  // Streaming replacement of the whole playlist
  bool beginUpload();
  void writeUpload(const uint8_t* data, size_t len);
  bool endUpload();
  void abortUpload();
  const char* getUploadError() const { return uploadError; }

  // JSONL line helpers
  static bool parseLine(const char* line, StreamInfo& out);
  static size_t formatLine(const StreamInfo& item, char* buf, size_t size);
};

#endif // PLAYLIST_H
