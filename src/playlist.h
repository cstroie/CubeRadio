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

// Constants for StreamInfo field sizes
#define STREAM_NAME_SIZE 96
#define STREAM_URL_SIZE 256   // Must be >= StreamInfoData::url[256] so URLs are never silently truncated

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

class Playlist {
private:
  StreamInfo playlist[MAX_PLAYLIST_SIZE];
  int count;

public:
  // Constructor
  Playlist();

  // Playlist management methods
  void load();
  void save();
  void setItem(int index, const char* name, const char* url);
  bool addItem(const char* name, const char* url);
  void removeItem(int index);
  void clear();

  // Getters
  int getCount() const;
  const StreamInfo& getItem(int index) const;

  // Utility methods
  void validate();
};

#endif // PLAYLIST_H
