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

/**
 * @brief Copy a stream name, truncating on a UTF-8 character boundary
 * @details Control characters (CR, LF, TAB, ...) are replaced with spaces so a
 * name always renders on a single line (OLED, MPD, HTML).
 */
static void copyName(char* dest, const char* src, size_t size) {
  size_t n = strlen(src);
  if (n >= size) {
    n = size - 1;
    // Back off to the lead byte of a multi-byte character cut by the limit
    while (n > 0 && (static_cast<uint8_t>(src[n]) & 0xC0) == 0x80) {
      n--;
    }
  }
  for (size_t i = 0; i < n; i++) {
    dest[i] = (static_cast<uint8_t>(src[i]) < 0x20) ? ' ' : src[i];
  }
  dest[n] = '\0';
}

/**
 * @brief Read one line from a file into a buffer
 * @param overflow Set when the line was longer than the buffer (the rest of
 *                 the line is consumed and discarded)
 * @return Line length without the line terminator
 */
static size_t readLine(File& file, char* buf, size_t size, bool& overflow) {
  size_t len = 0;
  overflow = false;
  int c;
  while ((c = file.read()) >= 0) {
    if (c == '\n') break;
    if (len < size - 1) {
      buf[len++] = static_cast<char>(c);
    } else {
      overflow = true;
    }
  }
  if (len > 0 && buf[len - 1] == '\r') len--;
  buf[len] = '\0';
  return len;
}

/**
 * @brief Check whether a line holds only whitespace
 */
static bool isBlank(const char* line) {
  for (const char* p = line; *p; p++) {
    if (!isspace(static_cast<uint8_t>(*p))) return false;
  }
  return true;
}

/**
 * @brief Playlist constructor
 */
Playlist::Playlist()
  : count(0), cacheIndex(-1), uploadLine(nullptr), uploadLineLen(0),
    uploadBytes(0), uploadCount(0), uploadLineNo(0), uploadActive(false),
    uploadOverflow(false), uploadLenient(false) {
  cache.name[0] = '\0';
  cache.url[0] = '\0';
  uploadError[0] = '\0';
}

/**
 * @brief Parse and validate one JSONL playlist line
 * @param line Null-terminated line, e.g. {"name":"Radio","url":"http://..."}
 * @param out Receives the entry when valid
 * @return true if the line is a valid playlist entry
 */
bool Playlist::parseLine(const char* line, StreamInfo& out) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return false;
  JsonObjectConst obj = doc.as<JsonObjectConst>();
  if (obj.isNull()) return false;
  const char* name = obj["name"] | "";
  const char* url  = obj["url"]  | "";
  if (name[0] == '\0' || !VALIDATE_URL(url)) return false;
  // A truncated URL would still look valid but never connect
  if (strlen(url) >= STREAM_URL_SIZE) return false;
  // URLs never contain whitespace or control characters
  for (const char* p = url; *p; p++) {
    if (static_cast<uint8_t>(*p) <= 0x20) return false;
  }
  copyName(out.name, name, STREAM_NAME_SIZE);
  SAFE_STRNCPY(out.url, url, STREAM_URL_SIZE);
  return true;
}

/**
 * @brief Serialize an entry as one JSONL line (without the newline)
 * @return Line length, or 0 if it does not fit in the buffer
 */
size_t Playlist::formatLine(const StreamInfo& item, char* buf, size_t size) {
  JsonDocument doc;
  doc["name"] = item.name;
  doc["url"]  = item.url;
  if (measureJson(doc) + 1 > size) return 0;
  return serializeJson(doc, buf, size);
}

/**
 * @brief Read the entry at an index from an open playlist file
 */
bool Playlist::readItem(File& file, int index, StreamInfo& out) const {
  if (index < 0 || index >= count) return false;
  if (!file.seek(offsets[index])) return false;
  char line[PLAYLIST_LINE_MAX];
  bool overflow;
  readLine(file, line, sizeof(line), overflow);
  return !overflow && parseLine(line, out);
}

/**
 * @brief Build the in-memory offset index from SPIFFS
 * @details Migrates the legacy JSON playlist once, recovers from a power loss
 * during a playlist swap, and skips (but keeps) invalid lines.
 */
void Playlist::load() {
  count = 0;
  cacheIndex = -1;
  // A power loss between the two renames of a swap leaves only the backup
  if (!SPIFFS.exists(PLAYLIST_FILE) && SPIFFS.exists(PLAYLIST_BAK_FILE)) {
    Serial.println("Restoring playlist from backup");
    SPIFFS.rename(PLAYLIST_BAK_FILE, PLAYLIST_FILE);
  }
  // Convert the pre-JSONL playlist; a successful migration reloads the index
  if (!SPIFFS.exists(PLAYLIST_FILE) && SPIFFS.exists(PLAYLIST_LEGACY_FILE)) {
    if (migrateLegacy()) return;
  }
  File file = SPIFFS.open(PLAYLIST_FILE, "r");
  if (!file) {
    Serial.println("No playlist found, continuing with empty playlist");
    return;
  }
  char line[PLAYLIST_LINE_MAX];
  StreamInfo item;
  int lineNo = 0;
  while (file.available()) {
    uint32_t pos = file.position();
    bool overflow;
    readLine(file, line, sizeof(line), overflow);
    lineNo++;
    if (!overflow && isBlank(line)) continue;
    if (overflow || !parseLine(line, item)) {
      Serial.printf("Warning: Skipping invalid playlist line %d\n", lineNo);
      continue;
    }
    if (count >= MAX_PLAYLIST_SIZE) {
      Serial.printf("Warning: Playlist limit reached (%d entries)\n", MAX_PLAYLIST_SIZE);
      break;
    }
    offsets[count++] = pos;
  }
  file.close();
  Serial.printf("Loaded %d streams from playlist\n", count);
}

/**
 * @brief Convert the legacy /playlist.json array into JSONL
 * @return true if the JSONL playlist was written and loaded
 */
bool Playlist::migrateLegacy() {
  Serial.println("Migrating playlist.json to playlist.jsonl");
  JsonDocument doc;
  if (!readJsonFile(PLAYLIST_LEGACY_FILE, 8192, doc) || !doc.is<JsonArray>()) {
    Serial.println("Legacy playlist unreadable, not migrated");
    return false;
  }
  if (!beginUpload()) return false;
  // Invalid legacy entries are dropped, as the old loader did
  uploadLenient = true;
  // Start with a blank line so an empty legacy array still migrates
  writeUpload(reinterpret_cast<const uint8_t*>("\n"), 1);
  char line[PLAYLIST_LINE_MAX];
  for (JsonVariantConst item : doc.as<JsonArrayConst>()) {
    size_t len = serializeJson(item, line, sizeof(line) - 1);
    if (len == 0 || len >= sizeof(line) - 1) continue;
    line[len++] = '\n';
    writeUpload(reinterpret_cast<const uint8_t*>(line), len);
  }
  if (!endUpload()) {
    Serial.printf("Playlist migration failed: %s\n", uploadError);
    return false;
  }
  SPIFFS.remove(PLAYLIST_LEGACY_FILE);
  SPIFFS.remove(String(PLAYLIST_LEGACY_FILE) + ".bak");
  return true;
}

/**
 * @brief Get the number of items in the playlist
 */
int Playlist::getCount() const {
  return count;
}

/**
 * @brief Get playlist item at specific index
 * @param index Playlist index (0-based)
 * @return Reference to a cached copy of the entry, or an empty item if out of
 * bounds. It is overwritten by the next getItem() call for another index.
 */
const StreamInfo& Playlist::getItem(int index) const {
  static const StreamInfo empty = {"", ""};
  if (index < 0 || index >= count) return empty;
  if (index == cacheIndex) return cache;
  File file = SPIFFS.open(PLAYLIST_FILE, "r");
  bool ok = file && readItem(file, index, cache);
  if (file) file.close();
  if (!ok) {
    cache.name[0] = '\0';
    cache.url[0] = '\0';
  }
  cacheIndex = index;
  return cache;
}

/**
 * @brief Visit all entries in order using a single open file
 * @param fn Called with (index, entry); return false to stop early
 */
void Playlist::forEach(const std::function<bool(int, const StreamInfo&)>& fn) const {
  if (count == 0) return;
  File file = SPIFFS.open(PLAYLIST_FILE, "r");
  if (!file) return;
  StreamInfo item;
  for (int i = 0; i < count; i++) {
    if (!readItem(file, i, item)) continue;
    if (!fn(i, item)) break;
  }
  file.close();
}

/**
 * @brief Find the first entry with the given URL
 * @return Index of the entry, or -1 if not found
 */
int Playlist::findByUrl(const char* url) const {
  int found = -1;
  if (!url || url[0] == '\0') return found;
  forEach([&](int i, const StreamInfo& item) {
    if (strcmp(item.url, url) == 0) {
      found = i;
      return false;
    }
    return true;
  });
  return found;
}

/**
 * @brief Start replacing the playlist with streamed JSONL data
 * @return true if the temporary file could be created
 */
bool Playlist::beginUpload() {
  releaseUpload();
  uploadError[0] = '\0';
  uploadLineLen = 0;
  uploadBytes = 0;
  uploadCount = 0;
  uploadLineNo = 0;
  uploadOverflow = false;
  uploadLenient = false;
  uploadLine = static_cast<char*>(malloc(PLAYLIST_LINE_MAX));
  if (!uploadLine) {
    failUpload("Out of memory");
    return false;
  }
  uploadFile = SPIFFS.open(PLAYLIST_TMP_FILE, "w");
  if (!uploadFile) {
    failUpload("Cannot create temporary playlist file");
    return false;
  }
  uploadActive = true;
  return true;
}

/**
 * @brief Feed a chunk of the uploaded JSONL body
 */
void Playlist::writeUpload(const uint8_t* data, size_t len) {
  if (!uploadActive) return;
  uploadBytes += len;
  for (size_t i = 0; i < len && uploadActive; i++) {
    char c = static_cast<char>(data[i]);
    if (c == '\n') {
      processUploadLine();
    } else if (uploadLineLen < PLAYLIST_LINE_MAX - 1) {
      uploadLine[uploadLineLen++] = c;
    } else {
      uploadOverflow = true;
    }
  }
}

/**
 * @brief Validate one complete uploaded line and append it to the temp file
 */
void Playlist::processUploadLine() {
  uploadLine[uploadLineLen] = '\0';
  uploadLineLen = 0;
  uploadLineNo++;
  bool overflow = uploadOverflow;
  uploadOverflow = false;
  if (!overflow && isBlank(uploadLine)) return;
  StreamInfo item;
  if (overflow || !parseLine(uploadLine, item)) {
    if (uploadLenient) return;
    char msg[sizeof(uploadError)];
    snprintf(msg, sizeof(msg), "Invalid playlist entry at line %d", uploadLineNo);
    failUpload(msg);
    return;
  }
  if (uploadCount >= MAX_PLAYLIST_SIZE) {
    char msg[sizeof(uploadError)];
    snprintf(msg, sizeof(msg), "Playlist exceeds maximum size (%d entries)", MAX_PLAYLIST_SIZE);
    failUpload(msg);
    return;
  }
  // Store a normalized line (the parsed entry is a copy, so reuse the buffer)
  size_t len = formatLine(item, uploadLine, PLAYLIST_LINE_MAX);
  if (len == 0 ||
      uploadFile.write(reinterpret_cast<const uint8_t*>(uploadLine), len) != len ||
      uploadFile.write('\n') != 1) {
    failUpload("Failed to write playlist (filesystem full?)");
    return;
  }
  uploadCount++;
}

/**
 * @brief Abort the upload, keeping the current playlist
 */
void Playlist::failUpload(const char* message) {
  SAFE_STRNCPY(uploadError, message, sizeof(uploadError));
  Serial.printf("Playlist upload failed: %s\n", uploadError);
  releaseUpload();
}

/**
 * @brief Free upload resources and delete the temporary file
 */
void Playlist::releaseUpload() {
  uploadActive = false;
  if (uploadFile) uploadFile.close();
  if (uploadLine) {
    free(uploadLine);
    uploadLine = nullptr;
  }
  if (SPIFFS.exists(PLAYLIST_TMP_FILE)) SPIFFS.remove(PLAYLIST_TMP_FILE);
}

/**
 * @brief Abort an upload (client disconnected)
 */
void Playlist::abortUpload() {
  if (uploadActive) failUpload("Upload aborted");
}

/**
 * @brief Finish the upload and swap the new playlist in
 * @return true if the new playlist replaced the old one
 */
bool Playlist::endUpload() {
  if (!uploadActive) {
    if (uploadError[0] == '\0') {
      SAFE_STRNCPY(uploadError, "No playlist data received", sizeof(uploadError));
    }
    return false;
  }
  if (uploadBytes == 0) {
    failUpload("Missing playlist data");
    return false;
  }
  // The last line may lack a trailing newline
  if (uploadLineLen > 0 || uploadOverflow) processUploadLine();
  if (!uploadActive) return false;
  uploadFile.close();
  free(uploadLine);
  uploadLine = nullptr;
  uploadActive = false;
  // Swap: current -> backup, temp -> current, then drop the backup
  SPIFFS.remove(PLAYLIST_BAK_FILE);
  if (SPIFFS.exists(PLAYLIST_FILE) && !SPIFFS.rename(PLAYLIST_FILE, PLAYLIST_BAK_FILE)) {
    failUpload("Cannot back up current playlist");
    return false;
  }
  if (!SPIFFS.rename(PLAYLIST_TMP_FILE, PLAYLIST_FILE)) {
    SPIFFS.rename(PLAYLIST_BAK_FILE, PLAYLIST_FILE);
    failUpload("Cannot replace playlist");
    return false;
  }
  SPIFFS.remove(PLAYLIST_BAK_FILE);
  Serial.printf("Saved playlist with %d streams\n", uploadCount);
  load();
  return true;
}
