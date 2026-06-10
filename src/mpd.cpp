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

#define MPD_VERSION "0.23.0"

#include "mpd.h"
#include "main.h"
#include "player.h"

/**
 * @brief Parse value from string, handling quotes
 * @details Extracts a numeric value from a string, removing surrounding whitespace
 * and quotes if present. This function is used to parse MPD command arguments
 * that may be quoted or unquoted.
 * 
 * The function implements robust argument parsing with the following features:
 * - Whitespace trimming from both ends of the string
 * - Quote removal for both single and double quotes
 * - Safe integer conversion with fallback to 0 for invalid values
 * 
 * @param valueStr The value string to parse
 * @return The parsed value as integer, or 0 if parsing fails
 */
int parseValue(const String& valueStr) {
  String cleanedStr = valueStr;
  // Remove whitespace
  cleanedStr.trim();
  // Remove quotes if present
  if (cleanedStr.startsWith("\"") && cleanedStr.endsWith("\"") && cleanedStr.length() >= 2) {
    cleanedStr = cleanedStr.substring(1, cleanedStr.length() - 1);
  }
  if (cleanedStr.startsWith("\'") && cleanedStr.endsWith("\'") && cleanedStr.length() >= 2) {
    cleanedStr = cleanedStr.substring(1, cleanedStr.length() - 1);
  }
  // Validate that string contains only digits (and optional minus sign)
  for (int i = 0; i < cleanedStr.length(); i++) {
    if ((i == 0 && cleanedStr[i] == '-') || isDigit(cleanedStr[i])) {
      continue;
    } else {
      return 0; // Return 0 for invalid input
    }
  }
  // Convert to integer
  return cleanedStr.toInt();
}

/**
 * @brief Handle the MPD stop command
 * @details This function processes the MPD "stop" command (and "pause" command which
 * is treated identically in this implementation). It stops the currently playing
 * stream and saves the player state.
 * 
 * The function performs the following actions:
 * 1. Calls stopStream() to stop audio playback
 * 2. Marks the player state as dirty to trigger persistence
 * 3. Saves the player state to non-volatile storage
 * 4. Sends an OK response to the MPD client
 * 
 * In MPD protocol terms, both "stop" and "pause" commands are handled by this
 * function for simplicity, as the CubeRadio implementation treats them the same way.
 * 
 * @param args Command arguments (not used for stop command)
 */
void MPDInterface::handleStopCommand(const String& args) {
  this->player.stopStream();
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD noidle command
 * @details This function processes the MPD "noidle" command by exiting
 * idle mode and returning to normal command processing. This allows
 * MPD clients to stop monitoring for changes and resume normal operation.
 * 
 * The function implements MPD protocol compatibility by:
 * - Clearing the inIdleMode flag to disable idle processing
 * - Returning standard OK response to acknowledge command
 * - Resuming normal command processing on next handleClient() call
 * 
 * This command is typically sent by MPD clients when they no longer
 * need to monitor for player state changes or when switching to
 * active command mode.
 * 
 * @param args Command arguments (not used for noidle command)
 */
void MPDInterface::handleNoIdleCommand(const String& args) {
  inIdleMode = false;
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD plchanges command
 * @details This function processes the MPD "plchanges" command by returning
 * information about playlist entries that have changed since a specified
 * playlist version. In this implementation, it returns all playlist entries
 * since detailed change tracking is not implemented.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning all playlist entries with full metadata
 * - Using standard response format for playlist changes
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not track detailed playlist changes since
 * the playlist is managed through the web interface. It simply returns
 * the entire playlist to maintain client compatibility.
 * 
 * Response information for each playlist entry includes:
 * - File URI (stream URL)
 * - Track title (stream name)
 * - Artist name (WebRadio)
 * - Album name (WebRadio)
 * - Entry ID (0-based index)
 * - Position (0-based index)
 * - Track number (1-based index)
 * - Last modified timestamp
 * 
 * @param args Command arguments (not used for plchanges command)
 */
void MPDInterface::handlePlChangesCommand(const String& args) {
  sendPlaylistInfo(3);
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD seekid command
 * @details This function processes the MPD "seekid" command which would
 * normally seek to a specific position in a track identified by ID.
 * In this implementation, the command is acknowledged but has no effect
 * since streaming playback does not support seeking.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not support seeking in streams since
 * web radio streams are live and do not have seekable positions.
 * The command is simply acknowledged to maintain client compatibility.
 * 
 * @param args Command arguments (not used for seekid command)
 */
void MPDInterface::handleSeekIdCommand(const String& args) {
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD seek command
 * @details This function processes the MPD "seek" command which would
 * normally seek to a specific position in a track. In this implementation,
 * the command is acknowledged but has no effect since streaming playback
 * does not support seeking.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not support seeking in streams since
 * web radio streams are live and do not have seekable positions.
 * The command is simply acknowledged to maintain client compatibility.
 * 
 * @param args Command arguments (not used for seek command)
 */
void MPDInterface::handleSeekCommand(const String& args) {
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD find command
 * @details This function processes the MPD "find" command by searching
 * for streams in the playlist that exactly match the specified criteria.
 * The search is case-insensitive and supports searching by title.
 * 
 * The function implements MPD protocol compatibility by:
 * - Performing case-insensitive exact matching
 * - Returning matching stream metadata
 * - Using standard response format for search results
 * 
 * Search behavior:
 * - Case-insensitive matching
 * - Exact string matching (equals)
 * - Supports title searches
 * - Returns file URI, title, track number, and last modified for matches
 * 
 * Special handling:
 * - Artist/album searches return all playlist entries
 * - Empty searches handled by existing search function
 * 
 * @param args Command arguments (search criteria)
 */
void MPDInterface::handleFindCommand(const String& args) {
  handleMPDSearchCommand(args, true);
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD search command
 * @details This function processes the MPD "search" command by searching
 * for streams in the playlist that partially match the specified criteria.
 * The search is case-insensitive and supports searching by title.
 * 
 * The function implements MPD protocol compatibility by:
 * - Performing case-insensitive partial matching
 * - Returning matching stream metadata
 * - Using standard response format for search results
 * 
 * Search behavior:
 * - Case-insensitive matching
 * - Partial string matching (contains)
 * - Supports title searches
 * - Returns file URI, title, track number, and last modified for matches
 * 
 * Special handling:
 * - Artist/album searches return all playlist entries
 * - Empty searches handled by existing search function
 * 
 * @param args Command arguments (search criteria)
 */
void MPDInterface::handleSearchCommand(const String& args) {
  handleMPDSearchCommand(args, false);
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD list command
 * @details This function processes the MPD "list" command by returning
 * values for a specific tag type. In this implementation, it supports
 * listing artists, albums, and titles from the playlist.
 * 
 * The function implements MPD protocol compatibility by:
 * - Parsing the requested tag type from command arguments
 * - Returning appropriate values for supported tag types
 * - Using dummy "WebRadio" values for artist/album tags
 * - Returning playlist titles when requested
 * 
 * Supported tag types:
 * - Artist: Returns "WebRadio" for all entries
 * - Album: Returns "WebRadio" for all entries
 * - Title: Returns all playlist stream names
 * 
 * Response format:
 * - One line per tag value in "TagType: value" format
 * - Standard OK response termination
 * 
 * This command allows MPD clients to browse available tag values for
 * filtering and search operations.
 * 
 * @param args Command arguments (tag type to list)
 */
void MPDInterface::handleListCommand(const String& args) {
  // Validate arguments length
  if (args.length() > 20) { // Reasonable limit for tag type
    mpdClient.print(mpdResponseError("list", "Tag type too long"));
    return;
  }
  // Parse tag type
  if (args.length() > 0) {
    String tagType = args;
    tagType.toLowerCase();
    tagType.trim();
    // Validate supported tag types
    if (!tagType.startsWith("artist") && !tagType.startsWith("album") && !tagType.startsWith("title")) {
      mpdClient.print(mpdResponseError("list", "Unsupported tag type"));
      return;
    }
    // Return values based on tag type
    if (tagType.startsWith("artist")) {
      // Return dummy artist
      mpdClient.print("Artist: WebRadio\n");
    } else if (tagType.startsWith("album")) {
      // Return dummy album
      mpdClient.print("Album: WebRadio\n");
    } else if (tagType.startsWith("title")) {
      // Return the playlist
      for (int i = 0; i < this->player.getPlaylistCount(); i++) {
        mpdClient.print("Title: " + String(this->player.getPlaylistItem(i).name) + "\n");
      }
    }
  } else {
    mpdClient.print(mpdResponseError("list", "Missing tag type"));
    return;
  }
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD listplaylists command
 * @details This function processes the MPD "listplaylists" command by
 * returning information about available playlists. In this implementation,
 * only a single default playlist (WebRadio) is available.
 * 
 * The function implements MPD protocol compatibility by:
 * - Reporting the single available playlist with metadata
 * - Including playlist name and last modified timestamp
 * - Using standard response format for playlist listings
 * 
 * Response information includes:
 * - Playlist name (WebRadio)
 * - Last modified timestamp (build time)
 * 
 * This command allows MPD clients to discover available playlists and
 * their metadata, which is useful for playlist management interfaces.
 * 
 * @param args Command arguments (not used for listplaylists command)
 */
void MPDInterface::handleListPlaylistsCommand(const String& args) {
  mpdClient.print("playlist: WebRadio\n");
  mpdClient.print("Last-Modified: " + String(BUILD_TIME) + "\n");
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD listplaylistinfo command
 * @details This function processes the MPD "listplaylistinfo" command by
 * returning minimal information about all streams in the playlist. This
 * command is typically used by MPD clients to retrieve playlist contents.
 * 
 * The function implements MPD protocol compatibility by:
 * - Providing minimal stream metadata for each playlist entry
 * - Including only file URI and title information
 * - Using the most basic detail level for efficient response
 * 
 * Response information for each playlist entry includes:
 * - File URI (stream URL)
 * - Track title (stream name)
 * 
 * This command is used by MPD clients when they need to retrieve the
 * contents of a playlist with minimal metadata, typically for display
 * in playlist views or selection interfaces.
 * 
 * @param args Command arguments (not used for listplaylistinfo command)
 */
void MPDInterface::handleListPlaylistInfoCommand(const String& args) {
  sendPlaylistInfo(0); // Minimal detail
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD listallinfo command
 * @details This function processes the MPD "listallinfo" command by returning
 * simple information about all streams in the playlist. This command is
 * typically used by MPD clients to browse all available media.
 * 
 * The function implements MPD protocol compatibility by:
 * - Providing basic stream metadata for each playlist entry
 * - Including file URI, title, track number, and last modified information
 * - Using minimal detail level for efficient response
 * 
 * Response information for each playlist entry includes:
 * - File URI (stream URL)
 * - Track title (stream name)
 * - Track number (1-based index)
 * - Last modified timestamp
 * 
 * This command is functionally similar to lsinfo but may be used in
 * different contexts by MPD clients for browsing available media.
 * 
 * @param args Command arguments (not used for listallinfo command)
 */
void MPDInterface::handleListAllInfoCommand(const String& args) {
  sendPlaylistInfo(1); // Simple detail
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD update command
 * @details This function processes the MPD "update" command which would
 * normally trigger a media database update. In this implementation, the
 * command is acknowledged but has no effect since there is no media database.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning "updating_db: 1" to indicate update in progress
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not have a media database to update.
 * The command is acknowledged to maintain client compatibility while
 * indicating that no actual update is occurring.
 * 
 * @param args Command arguments (not used for update command)
 */
void MPDInterface::handleUpdateCommand(const String& args) {
  mpdClient.print("updating_db: 1\n");
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD password command
 * @details This function processes the MPD "password" command which would
 * normally be used for authentication. In this implementation, the command
 * is acknowledged but has no effect since authentication is not implemented.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not implement password authentication.
 * The command is simply acknowledged to maintain client compatibility.
 * 
 * @param args Command arguments (not used for password command)
 */
void MPDInterface::handlePasswordCommand(const String& args) {
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD ping command
 * @details This function processes the MPD "ping" command which is used by
 * clients to test connectivity and keep the connection alive. The command
 * simply returns OK to acknowledge the ping.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * This command is typically sent periodically by MPD clients to ensure
 * the server is still responsive and to prevent connection timeouts.
 * 
 * @param args Command arguments (not used for ping command)
 */
void MPDInterface::handlePingCommand(const String& args) {
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD stats command
 * @details This function processes the MPD "stats" command by returning
 * statistics about the player's operation including uptime, playback time,
 * and media counts.
 * 
 * The function implements MPD protocol compatibility by:
 * - Calculating uptime from system start time
 * - Tracking total and current playback time
 * - Reporting media counts (artists, albums, songs)
 * - Including database update timestamp
 * 
 * Response information includes:
 * - Artists count (1 for WebRadio)
 * - Albums count (1 for WebRadio)
 * - Songs count (playlist length)
 * - Uptime in seconds
 * - Total play time in seconds
 * - Database play time in seconds
 * - Database update timestamp (build time)
 * 
 * Statistics tracking:
 * - Uptime calculated from system start
 * - Play time includes both historical and current playback
 * - Media counts reflect current playlist state
 * - Database timestamp uses build time
 * 
 * @param args Command arguments (not used for stats command)
 */
void MPDInterface::handleStatsCommand(const String& args) {
  // Calculate uptime and playtime
  unsigned long uptime = (millis() / 1000);
  unsigned long playtime = this->player.getTotalPlayTime();
  if (this->player.isPlaying() && this->player.getPlayStartTime() > 0) {
    // playStartTime is in milliseconds; unsigned subtraction handles rollover
    playtime += (millis() - this->player.getPlayStartTime()) / 1000;
  }
  // Send stats information
  mpdClient.print("artists: 1\n");
  mpdClient.print("albums: 1\n");
  mpdClient.print("songs: " + String(this->player.getPlaylistCount()) + "\n");
  mpdClient.print("uptime: " + String(uptime) + "\n");
  mpdClient.print("playtime: " + String(playtime) + "\n");
  mpdClient.print("db_playtime: " + String(playtime) + "\n");
  mpdClient.print("db_update: 0\n");
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD notcommands command
 * @details This function processes the MPD "notcommands" command which would
 * normally return a list of commands that are not available. In this
 * implementation, the command simply returns OK since all commands are
 * technically "available" (even if some are no-ops).
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not actually track disabled commands since
 * all registered commands are available (even if some have no effect).
 * The command is simply acknowledged to maintain client compatibility.
 * 
 * @param args Command arguments (not used for notcommands command)
 */
void MPDInterface::handleNotCommandsCommand(const String& args) {
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD commands command
 * @details This function processes the MPD "commands" command by returning a
 * list of all supported MPD commands. This allows MPD clients to understand
 * which commands are available for use with this implementation.
 * 
 * The function implements MPD protocol compatibility by:
 * - Returning the list of supported commands in standard format
 * - Using "command: " prefix for each supported command
 * - Including all commands registered in the supportedCommands vector
 * - Maintaining compatibility with MPD clients that query capabilities
 * 
 * Response format:
 * - One "command: command_name" line for each supported command
 * - Standard OK response termination
 * 
 * The supportedCommands vector is initialized in the constructor and contains
 * all commands that this implementation handles, either fully or partially.
 * 
 * @param args Command arguments (not used for commands command)
 */
void MPDInterface::handleCommandsCommand(const String& args) {
  for (const auto& cmd : supportedCommands) {
    mpdClient.print("command: ");
    mpdClient.print(cmd.c_str());
    mpdClient.print("\n");
    yield();
  }
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD outputs command
 * @details This function processes the MPD "outputs" command by returning
 * information about available audio outputs. Since the CubeRadio uses
 * ESP32-audioI2S, only a single I2S output is available.
 * 
 * The function implements MPD protocol compatibility by:
 * - Reporting the single I2S output with appropriate metadata
 * - Using standard output ID, name, and enabled status format
 * - Maintaining compatibility with MPD clients that query outputs
 * 
 * Response information includes:
 * - Output ID (0 for I2S output)
 * - Output name (I2S (External DAC))
 * - Enabled status (1 for enabled)
 * 
 * This command allows MPD clients to understand the available audio
 * output options and their current status.
 * 
 * @param args Command arguments (not used for outputs command)
 */
void MPDInterface::handleOutputsCommand(const String& args) {
  mpdClient.print("outputid: 0\n");
  mpdClient.print("outputname: I2S (External DAC)\n");
  mpdClient.print("outputenabled: 1\n");
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD save command
 * @details This function processes the MPD "save" command which would normally
 * save the current playlist. In this implementation, the command is acknowledged
 * but has no effect since playlist management is handled through the web interface.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not actually save playlists since
 * playlist management is performed through the web interface. The command
 * is simply acknowledged to maintain client compatibility.
 * 
 * @param args Command arguments (not used for save command)
 */
void MPDInterface::handleSaveCommand(const String& args) {
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD load command
 * @details This function processes the MPD "load" command which would normally
 * load a stored playlist. In this implementation, the command is acknowledged
 * but has no effect since playlist management is handled through the web interface.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not actually load playlists since
 * playlist management is performed through the web interface. The command
 * is simply acknowledged to maintain client compatibility.
 * 
 * @param args Command arguments (not used for load command)
 */
void MPDInterface::handleLoadCommand(const String& args) {
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD delete command
 * @details This function processes the MPD "delete" command which would normally
 * remove a stream from the current playlist. In this implementation, the command
 * is acknowledged but has no effect since playlist management is handled through
 * the web interface.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not actually delete streams from the playlist since
 * playlist management is performed through the web interface. The command
 * is simply acknowledged to maintain client compatibility.
 * 
 * @param args Command arguments (not used for delete command)
 */
void MPDInterface::handleDeleteCommand(const String& args) {
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD add command
 * @details This function processes the MPD "add" command which would normally
 * add a stream to the current playlist. In this implementation, the command is
 * acknowledged but has no effect since playlist management is handled through
 * the web interface.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not actually add streams to the playlist since
 * playlist management is performed through the web interface. The command
 * is simply acknowledged to maintain client compatibility.
 * 
 * @param args Command arguments (not used for add command)
 */
void MPDInterface::handleAddCommand(const String& args) {
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD clear command
 * @details This function processes the MPD "clear" command which would normally
 * clear the current playlist. In this implementation, the command is acknowledged
 * but has no effect since playlist management is handled through the web interface.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not actually clear the playlist since
 * playlist management is performed through the web interface. The command
 * is simply acknowledged to maintain client compatibility.
 * 
 * @param args Command arguments (not used for clear command)
 */
void MPDInterface::handleClearCommand(const String& args) {
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD getvol command
 * @details This function processes the MPD "getvol" command by returning the
 * current playback volume as a percentage value. The volume is converted from
 * the ESP32-audioI2S 0-22 scale to MPD's 0-100 percentage scale.
 * 
 * The function implements MPD protocol compatibility by:
 * - Converting volume from ESP32-audioI2S 0-22 scale to MPD 0-100 percentage
 * - Returning volume in the standard "volume: value" format
 * - Supporting clients that query current volume settings
 * 
 * Response format:
 * - "volume: X\n" where X is the current volume percentage (0-100)
 * - Standard OK response termination
 * 
 * This command is typically used by MPD clients to display or synchronize
 * volume controls with the current player state.
 * 
 * @param args Command arguments (not used for getvol command)
 */
void MPDInterface::handleGetVolCommand(const String& args) {
  int volPercent = map(this->player.getVolume(), 0, 22, 0, 100);
  mpdClient.print("volume: " + String(volPercent) + "\n");
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD lsinfo command
 * @details This function processes the MPD "lsinfo" command by returning
 * simple information about all streams in the playlist. This command is
 * typically used by MPD clients to browse available media.
 * 
 * The function implements MPD protocol compatibility by:
 * - Providing basic stream metadata for each playlist entry
 * - Including file URI, title, track number, and last modified information
 * - Using minimal detail level for efficient response
 * 
 * Response information for each playlist entry includes:
 * - File URI (stream URL)
 * - Track title (stream name)
 * - Track number (1-based index)
 * - Last modified timestamp
 * 
 * This command is functionally similar to listallinfo but with simpler
 * metadata, making it suitable for browsing interfaces.
 * 
 * @param args Command arguments (not used for lsinfo command)
 */
void MPDInterface::handleLsInfoCommand(const String& args) {
  sendPlaylistInfo(1); // Simple detail
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD playlistid command
 * @details This function processes the MPD "playlistid" command by returning
 * information about a specific stream in the playlist identified by its ID.
 * If no ID is provided, it returns information about all streams.
 * 
 * The function implements MPD protocol compatibility by:
 * - Validating playlist IDs against current playlist bounds
 * - Providing appropriate error responses for invalid IDs
 * - Returning comprehensive metadata for valid entries
 * - Supporting both specific ID and all-entries modes
 * 
 * For valid IDs, the response includes:
 * - File URI (stream URL)
 * - Track title (stream name)
 * - Artist name (WebRadio)
 * - Album name (WebRadio)
 * - Entry ID (0-based index)
 * - Position (0-based index)
 * 
 * Error handling:
 * - Returns ACK error for IDs outside playlist bounds
 * - Returns full playlist info when no ID specified
 * 
 * @param args Command arguments (optional playlist ID)
 */
void MPDInterface::handlePlaylistIdCommand(const String& args) {
  int id = -1;
  if (args.length() > 0) {
    id = parseValue(args);
    // Validate ID range
    if (id < 0 || id >= this->player.getPlaylistCount()) {
      mpdClient.print(mpdResponseError("playlistid", "Invalid playlist ID"));
      return;
    }
  }
  // Check if the ID is valid
  if (id >= 0 && id < this->player.getPlaylistCount()) {
    // Return specific entry
    const StreamInfo& item = this->player.getPlaylistItem(id);
    mpdClient.print("file: " + String(item.url) + "\n");
    mpdClient.print("Title: " + String(item.name) + "\n");
    mpdClient.print("Artist: WebRadio\n");
    mpdClient.print("Album: WebRadio\n");
    mpdClient.print("Id: " + String(id) + "\n");
    mpdClient.print("Pos: " + String(id) + "\n");
  } else {
    // Return all if no specific ID
    sendPlaylistInfo(3);
  }
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD playlistinfo command
 * @details This function processes the MPD "playlistinfo" command by returning
 * detailed information about all streams in the playlist with full metadata.
 * 
 * The function implements MPD protocol compatibility by:
 * - Providing comprehensive stream metadata for each playlist entry
 * - Including file URI, title, ID, position, and last modified information
 * - Supporting artist/album metadata with dummy "WebRadio" values
 * 
 * Response information for each playlist entry includes:
 * - File URI (stream URL)
 * - Track title (stream name)
 * - Artist name (WebRadio)
 * - Album name (WebRadio)
 * - Entry ID (0-based index)
 * - Position (0-based index)
 * - Track number (1-based index)
 * - Last modified timestamp
 * 
 * @param args Command arguments (not used for playlistinfo command)
 */
void MPDInterface::handlePlaylistInfoCommand(const String& args) {
  sendPlaylistInfo(3);
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD currentsong command
 * @details This function processes the MPD "currentsong" command by returning
 * detailed information about the currently playing stream. The response includes
 * file URI, track metadata, and position information when a stream is playing.
 * 
 * The function implements MPD protocol compatibility by:
 * - Providing file URI as the stream URL
 * - Parsing stream title for artist/track information when available
 * - Using stream name as fallback when no title is available
 * - Reporting position and ID information using 1-based indexing
 * 
 * Metadata parsing features:
 * - Automatic detection of "Artist - Title" format in stream titles
 * - Proper separation of artist and title fields when separator is found
 * - Fallback to using full title as track name when no separator exists
 * - Use of stream name as ultimate fallback when no title is available
 * 
 * Response information includes:
 * - File URI (stream URL)
 * - Artist name (parsed from title or "WebRadio" default)
 * - Track title (parsed from title or stream name)
 * - Position (0-based index)
 * - ID (0-based index)
 * 
 * @param args Command arguments (not used for currentsong command)
 */
void MPDInterface::handleCurrentSongCommand(const String& args) {
  if (this->player.isPlaying() && strlen(this->player.getStreamName()) > 0) {
    mpdClient.print("file: " + String(this->player.getStreamUrl()) + "\n");
    if (strlen(this->player.getStreamTitle()) > 0) {
      String streamTitleStr = String(this->player.getStreamTitle());
      // Check if stream title contains " - " separator for artist/track parsing
      int separatorPos = streamTitleStr.indexOf(" - ");
      if (separatorPos != -1) {
        // Split into artist and track using the " - " separator
        String artist = streamTitleStr.substring(0, separatorPos);
        String title = streamTitleStr.substring(separatorPos + 3); // Skip " - "
        mpdClient.print("Artist: " + artist + "\n");
        mpdClient.print("Title: " + title + "\n");
      } else {
        // No separator, use full title as track name
        mpdClient.print("Title: " + streamTitleStr + "\n");
      }
    } else {
      // No stream title, use stream name as fallback
      mpdClient.print("Title: " + String(this->player.getStreamName()) + "\n");
    }
    mpdClient.print("Album: WebRadio\n");
    mpdClient.print("Id: " + String(this->player.getPlaylistIndex()) + "\n");
    mpdClient.print("Pos: " + String(this->player.getPlaylistIndex()) + "\n");
    mpdClient.print("Track: " + String(this->player.getPlaylistIndex() + 1) + "\n");
  }
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD status command
 * @details This function processes the MPD "status" command by returning comprehensive
 * information about the current player state. The response includes volume settings,
 * playback state, playlist information, and stream metadata when applicable.
 * 
 * The function implements MPD protocol compatibility by:
 * - Converting volume from ESP32-audioI2S 0-22 scale to MPD 0-100 percentage scale
 * - Reporting standard MPD state values (play, stop, pause)
 * - Providing playlist position and ID information
 * - Including stream metadata like bitrate and audio format when playing
 * - Reporting elapsed time since playback started
 * 
 * Status information includes:
 * - Volume level (0-100)
 * - Repeat, random, single, consume modes (all 0 for disabled in this implementation)
 * - Playlist version and length
 * - Current playback state (play/stop)
 * - Song position and ID when playing
 * - Elapsed time in seconds when playing
 * - Bitrate and audio format when playing
 * - Next song information
 * - Database update status
 * 
 * @param args Command arguments (not used for status command)
 */
void MPDInterface::handleStatusCommand(const String& args) {
  int index = this->player.getPlaylistIndex();
  int volPercent = map(this->player.getVolume(), 0, 22, 0, 100);
  mpdClient.print("volume: " + String(volPercent) + "\n");
  mpdClient.print("repeat: 0\n");
  mpdClient.print("random: 0\n");
  mpdClient.print("single: 0\n");
  mpdClient.print("consume: 0\n");
  mpdClient.print("playlist: 1\n");
  mpdClient.print("playlistlength: " + String(this->player.getPlaylistCount()) + "\n");
  mpdClient.print("mixrampdb: 0.000000\n");
  mpdClient.print("state: " + String(this->player.isPlaying() ? "play" : "stop") + "\n");
  if (this->player.isPlaying() && strlen(this->player.getStreamName()) > 0) {
    mpdClient.print("song: " + String(index) + "\n");
    mpdClient.print("songid: " + String(index) + "\n");
    // Calculate elapsed time since playback started
    unsigned long elapsed = 0;
    if (this->player.getPlayStartTime() > 0) {
      // playStartTime is in milliseconds; unsigned subtraction handles rollover
      elapsed = (millis() - this->player.getPlayStartTime()) / 1000;
    }
    mpdClient.print("elapsed: " + String(elapsed) + ".000\n");
    mpdClient.print("bitrate: " + String(this->player.getBitrate()) + "\n");
    mpdClient.print("audio: 44100:16:2\n");
    if (++index >= this->player.getPlaylistCount()) {
      index = 0; // Wrap around to start
    }
    mpdClient.print("nextsong: " + String(index) + "\n");
    mpdClient.print("nextsongid: " + String(index) + "\n");
  }
  mpdClient.print("updating_db: 0\n");
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Handle the MPD tagtypes command
 * @details This function processes the MPD "tagtypes" command by returning
 * a list of supported tag types that can be used for searching and filtering.
 * 
 * The function implements MPD protocol compatibility by:
 * - Supporting special "all" and "clear" arguments
 * - Returning the list of supported tag types in standard format
 * - Using "tagtype: " prefix for each supported tag type
 * - Including all tag types registered in the supportedTagTypes vector
 * 
 * Response format:
 * - One "tagtype: tag_type" line for each supported tag type
 * - Standard OK response termination
 * 
 * Special arguments:
 * - "all": Returns OK without listing tag types
 * - "clear": Returns OK without listing tag types
 * 
 * The supportedTagTypes vector is initialized in the constructor and contains
 * all tag types that this implementation supports for search operations.
 * 
 * @param args Command arguments (optional "all" or "clear")
 */
void MPDInterface::handleTagTypesCommand(const String& args) {
  if (args.equals("\"all\"") || args.equals("\"clear\"")) {
    // These commands simply return OK
    mpdClient.print(mpdResponseOK());
  } else {
    // Send the list of supported tag types
    for (const auto& tagType : supportedTagTypes) {
      mpdClient.print("tagtype: ");
      mpdClient.print(tagType.c_str());
      mpdClient.print("\n");
      yield();
    }
    mpdClient.print(mpdResponseOK());
  }
}

/**
 * @brief Handle the MPD enableoutput command
 * @details This function processes the MPD "enableoutput" command which would
 * normally enable a specific audio output. In this implementation, the command
 * is acknowledged but has no effect since the I2S output is always enabled.
 * 
 * The function implements MPD protocol compatibility by:
 * - Validating the output ID (only 0 is supported)
 * - Accepting the command without error for valid IDs
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not actually enable outputs since
 * the I2S output is always enabled. The command is simply acknowledged
 * to maintain client compatibility.
 * 
 * Error handling:
 * - Returns ACK error for invalid output IDs (not 0)
 * - Returns ACK error for missing output ID
 * 
 * @param args Command arguments (output ID to enable)
 */
void MPDInterface::handleEnableOutputCommand(const String& args) {
  if (args.length() > 0) {
    int outputId = parseValue(args);
    if (outputId == 0) {
      // Only output 0 (I2S) is supported with ESP32-audioI2S
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("enableoutput", "Invalid output ID"));
      return;
    }
  } else {
    mpdClient.print(mpdResponseError("enableoutput", "Missing output ID"));
    return;
  }
}

/**
 * @brief Handle the MPD disableoutput command
 * @details This function processes the MPD "disableoutput" command which would
 * normally disable a specific audio output. In this implementation, the command
 * is acknowledged but has no effect since only I2S output is available and it
 * cannot be disabled.
 * 
 * The function implements MPD protocol compatibility by:
 * - Validating the output ID (only 0 is supported)
 * - Accepting the command without error for valid IDs
 * - Returning standard OK response
 * - Maintaining compatibility with MPD clients
 * 
 * Note: This implementation does not actually disable outputs since
 * the I2S output is always enabled. The command is simply acknowledged
 * to maintain client compatibility.
 * 
 * Error handling:
 * - Returns ACK error for invalid output IDs (not 0)
 * - Returns ACK error for missing output ID
 * 
 * @param args Command arguments (output ID to disable)
 */
void MPDInterface::handleDisableOutputCommand(const String& args) {
  if (args.length() > 0) {
    int outputId = parseValue(args);
    // Validate output ID (only 0 is supported)
    if (outputId != 0) {
      mpdClient.print(mpdResponseError("disableoutput", "Invalid output ID"));
      return;
    }
    // We don't actually disable outputs, just acknowledge the command
    mpdClient.print(mpdResponseOK());
  } else {
    mpdClient.print(mpdResponseError("disableoutput", "Missing output ID"));
    return;
  }
}

/**
 * @brief Handle the MPD previous command
 * @details This function processes the MPD "previous" command by moving playback
 * to the previous stream in the playlist. If currently at the first stream, it
 * wraps around to the last stream.
 * 
 * The function implements MPD protocol compatibility by:
 * - Calculating previous playlist index with wraparound behavior
 * - Validating playlist exists before attempting navigation
 * - Providing appropriate error responses for empty playlists
 * - Updating global state and persisting player settings
 * 
 * Navigation behavior:
 * - Moves to previous stream (index - 1 with wraparound)
 * - Wraps to last stream when at beginning of playlist
 * - Maintains current selection state
 * - Starts playback of new stream
 * 
 * Error handling:
 * - Returns ACK error for empty playlists
 * - Returns ACK error for playback failures
 * 
 * @param args Command arguments (not used for previous command)
 */
void MPDInterface::handlePreviousCommand(const String& args) {
  if (this->player.getPlaylistCount() > 0) {
    int prevIndex = (this->player.getPlaylistIndex() - 1 + this->player.getPlaylistCount()) % this->player.getPlaylistCount();
    if (handlePlayback(prevIndex)) {
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("previous", "Playback failed"));
      return;
    }
  } else {
    mpdClient.print(mpdResponseError("previous", "No playlist"));
    return;
  }
}

/**
 * @brief Handle the MPD next command
 * @details This function processes the MPD "next" command by advancing playback
 * to the next stream in the playlist. If currently at the last stream, it wraps
 * around to the first stream.
 * 
 * The function implements MPD protocol compatibility by:
 * - Calculating next playlist index with wraparound behavior
 * - Validating playlist exists before attempting navigation
 * - Providing appropriate error responses for empty playlists
 * - Updating global state and persisting player settings
 * 
 * Navigation behavior:
 * - Advances to next stream (index + 1)
 * - Wraps to first stream when at end of playlist
 * - Maintains current selection state
 * - Starts playback of new stream
 * 
 * Error handling:
 * - Returns ACK error for empty playlists
 * - Returns ACK error for playback failures
 * 
 * @param args Command arguments (not used for next command)
 */
void MPDInterface::handleNextCommand(const String& args) {
  // Next command
  if (this->player.getPlaylistCount() > 0) {
    int nextIndex = (this->player.getPlaylistIndex() + 1) % this->player.getPlaylistCount();
    if (handlePlayback(nextIndex)) {
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("next", "Playback failed"));
      return;
    }
  } else {
    mpdClient.print(mpdResponseError("next", "No playlist"));
    return;
  }
}

/**
 * @brief Handle the MPD volume command
 * @details This function processes the MPD "volume" command by adjusting the
 * playback volume by a relative amount. The volume change can be positive
 * (increase) or negative (decrease) and is applied to the current volume.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting relative volume changes (positive or negative integers)
 * - Converting current volume to MPD's 0-100 percentage scale for calculations
 * - Clamping resulting volume to valid 0-100 range
 * - Converting back to ESP32-audioI2S 0-22 scale for hardware control
 * - Updating UI and notifying WebSocket clients of changes
 * 
 * Volume adjustment features:
 * - Relative change support (positive/negative values)
 * - Range clamping (0-100 percentage, 0-22 hardware scale)
 * - Real-time audio volume adjustment
 * - UI synchronization through updateDisplay()
 * - WebSocket notifications for remote clients
 * 
 * Error handling:
 * - Returns ACK error for missing volume change values
 * 
 * @param args Command arguments (relative volume change)
 */
void MPDInterface::handleVolumeCommand(const String& args) {
  // Validate arguments length
  if (args.length() > 5) { // Reasonable limit for volume change value
    mpdClient.print(mpdResponseError("volume", "Invalid argument length"));
    return;
  }
  // Volume command - change volume by relative amount
  if (args.length() > 0) {
    // Parse volume change value (can be negative for decrease)
    int volumeChange = parseValue(args);
    // Validate volume change range (reasonable limits)
    if (volumeChange < -100 || volumeChange > 100) {
      mpdClient.print(mpdResponseError("volume", "Volume change out of range"));
      return;
    }
    // Get volume change as value for MPD compatibility
    int volumeChangeMPD = map(abs(volumeChange), 0, 100, 0, 22);
    // If the volume change is less than 1 after mapping, use 1 to ensure a change
    if (volumeChangeMPD < 1)
      volumeChangeMPD = 1;
    // Apply the original sign to the mapped value
      if (volumeChange < 0)
        volumeChangeMPD = -volumeChangeMPD;
    // Apply the volume change
    this->player.setVolume(this->player.getVolume() + volumeChangeMPD);
    updateDisplay();
    sendStatusToClients();  // Notify WebSocket clients of volume change
    mpdClient.print(mpdResponseOK());
  } else {
    mpdClient.print(mpdResponseError("volume", "Missing volume change value"));
    return;
  }
}

/**
 * @brief Handle the MPD setvol command
 * @details This function processes the MPD "setvol" command by setting the
 * playback volume to an absolute value. The volume is specified as a percentage
 * (0-100) which is converted to the ESP32-audioI2S 0-22 scale.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting volume values in MPD's 0-100 percentage scale
 * - Converting values to ESP32-audioI2S 0-22 scale for hardware control
 * - Validating volume values against acceptable range
 * - Updating UI and notifying WebSocket clients of changes
 * - Providing appropriate error responses for invalid values
 * 
 * Volume control features:
 * - Range validation (0-100 for MPD compatibility)
 * - Automatic scaling to 0-22 for ESP32-audioI2S
 * - Real-time audio volume adjustment
 * - UI synchronization through updateDisplay()
 * - WebSocket notifications for remote clients
 * 
 * Error handling:
 * - Returns ACK error for missing volume values
 * - Returns ACK error for out-of-range volume values
 * 
 * @param args Command arguments (volume percentage 0-100)
 */
void MPDInterface::handleSetVolCommand(const String& args) {
  // Validate arguments length
  if (args.length() > 5) { // Reasonable limit for volume value (0-100)
    mpdClient.print(mpdResponseError("setvol", "Invalid argument length"));
    return;
  }
  // Set volume command
  if (args.length() > 0) {
    // Parse volume value, handling quotes if present
    int newVolume = parseValue(args);
    // Validate volume range (0-100 for MPD compatibility)
    if (newVolume >= 0 && newVolume <= 100) {
      int mpdVol = map(this->player.getVolume(), 0, 22, 0, 100);
      // Convert from MPD's 0-100 scale to ESP32-audioI2S 0-22 scale
      int volume = map(newVolume, 0, 100, 0, 22);
      // If the volume stays the same after mapping, ensure a change
      if (volume == this->player.getVolume()) {
        if (newVolume < mpdVol)
          volume -= 1;
        else if (newVolume > mpdVol)
          volume += 1;
      }
      // Apply the new volume
      this->player.setVolume(volume);
      updateDisplay();
      sendStatusToClients();  // Notify WebSocket clients of volume change
      mpdClient.print(mpdResponseOK());
    } else {
      mpdClient.print(mpdResponseError("setvol", "Volume out of range"));
      return;
    }
  } else {
    mpdClient.print(mpdResponseError("setvol", "Missing volume value"));
    return;
  }
}

/**
 * @brief Handle the MPD play and playid commands
 * @details This function processes the MPD "play" and "playid" commands by
 * starting playback of a stream from the playlist. The command can specify
 * either a playlist index (play) or playlist ID (playid), with -1 indicating
 * the current selection.
 * 
 * The function implements MPD protocol compatibility by:
 * - Supporting both play (by index) and playid (by ID) commands
 * - Validating playlist indices against current playlist bounds
 * - Providing appropriate error responses for invalid indices
 * - Handling both explicit and implicit stream selection
 * - Updating global state and persisting player settings
 * 
 * Playback flow:
 * 1. Parse and validate playlist index/ID from arguments
 * 2. Use current selection if no valid index provided
 * 3. Validate index is within playlist bounds
 * 4. Update current selection
 * 5. Start stream playback
 * 6. Mark player state as dirty and save
 * 
 * Error handling:
 * - Returns ACK error for empty playlists
 * - Returns ACK error for indices outside playlist bounds
 * - Returns ACK error for playback failures
 * 
 * @param args Command arguments (optional playlist index/ID)
 */
void MPDInterface::handlePlayCommand(const String& args) {
  // Validate arguments length
  if (args.length() > 10) { // Reasonable limit for playlist index
    mpdClient.print(mpdResponseError("play", "Invalid argument length"));
    return;
  }
  // Play command - optional playlist index
  int playlistIndex = -1;
  if (args.length() > 0) {
    // Convert to 0-based index
    playlistIndex = parseValue(args);
    // Validate index if provided
    if (playlistIndex < -1 || playlistIndex >= this->player.getPlaylistCount()) {
      mpdClient.print(mpdResponseError("play", "Invalid playlist index"));
      return;
    }
  }
  // If no index provided, use current selection
  if (handlePlayback(playlistIndex)) {
    mpdClient.print(mpdResponseOK());
  } else {
    mpdClient.print(mpdResponseError("play", "No playlist"));
    return;
  }
}

/**
 * @brief Handle the MPD kill command
 * @details This function processes the MPD "kill" command by restarting the
 * ESP32 device. This provides a way for MPD clients to trigger a system
 * restart through the standard MPD protocol.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response before restart
 * - Flushing the client connection before restart
 * - Using ESP.restart() to perform system restart
 * 
 * Restart behavior:
 * - Sends OK response to acknowledge command
 * - Flushes client connection to ensure delivery
 * - Calls ESP.restart() to reboot the device
 * 
 * This command provides MPD clients with a standard way to restart the
 * CubeRadio device without requiring direct access to the web interface.
 * 
 * @param args Command arguments (not used for kill command)
 */
void MPDInterface::handleKillCommand(const String& args) {
  mpdClient.print(mpdResponseOK());
  mpdClient.flush();
  // Use ESP32 restart function
  ESP.restart();
}

/**
 * @brief Handle the MPD idle command
 * @details This function processes the MPD "idle" command by putting the
 * connection into idle mode where it waits for changes in player state
 * before sending notifications. This allows MPD clients to efficiently
 * monitor player status without polling.
 * 
 * The function implements MPD protocol compatibility by:
 * - Setting the inIdleMode flag to enable idle processing
 * - Initializing hash values for change detection
 * - Computing initial hashes of stream title and player status
 * - Suspending normal command processing until changes occur
 * 
 * Idle mode behavior:
 * - Monitors stream title for playlist changes
 * - Monitors player status (playing, volume, bitrate) for player changes
 * - Sends "changed: " notifications when changes are detected
 * - Supports noidle command to exit idle mode
 * 
 * Change detection uses hash-based comparison for efficiency:
 * - Title hash: polynomial rolling hash of stream title string
 * - Status hash: combined hash of playing status, volume, and bitrate
 * 
 * @param args Command arguments (not used for idle command)
 */
void MPDInterface::handleIdleCommand(const String& args) {
  inIdleMode = true;
  // Initialize hashes for tracking changes
  lastTitleHash = 0;
  for (int i = 0; this->player.getStreamTitle()[i]; i++) {
    lastTitleHash = lastTitleHash * 31 + this->player.getStreamTitle()[i];
  }
  lastStatusHash = this->player.isPlaying() ? 1 : 0;
  lastStatusHash = lastStatusHash * 31 + this->player.getVolume();
  // Don't send immediate response - wait for changes
}

/**
 * @brief Handle the MPD close command
 * @details This function processes the MPD "close" command by closing
 * the current client connection. This allows MPD clients to gracefully
 * terminate their connection to the server.
 * 
 * The function implements MPD protocol compatibility by:
 * - Accepting the command without error
 * - Returning standard OK response before closing
 * - Closing the client connection using mpdClient.stop()
 * - Cleaning up connection state for next client
 * 
 * Connection cleanup:
 * - Sends OK response to acknowledge command
 * - Calls mpdClient.stop() to close connection
 * - Resets command list and idle mode state
 * - Clears command buffer for next connection
 * 
 * This command provides MPD clients with a standard way to terminate
 * their connection without simply dropping the TCP connection.
 * 
 * @param args Command arguments (not used for close command)
 */
void MPDInterface::handleCloseCommand(const String& args) {
  // Close command
  mpdClient.print(mpdResponseOK());
  mpdClient.flush();
  // Close the client connection
  mpdClient.stop();
}

/**
 * @brief Handle the MPD command_list_begin command
 * @details This function is called when the command_list_begin command is received.
 * It puts the MPD interface into command list mode where commands are buffered
 * until command_list_end is received.
 * 
 * In command list mode, commands are not executed immediately but are stored
 * in a buffer. When command_list_end is received, all buffered commands are
 * executed sequentially. This allows clients to send multiple commands as a
 * single atomic operation.
 * 
 * The function sets the following state variables:
 * - inCommandList: true to indicate command list mode is active
 * - commandListOK: false to indicate standard responses should be used
 * - commandListCount: 0 to reset the command counter
 * 
 * The actual command list processing is handled by the handleCommandList function
 * when command_list_end is received.
 * 
 * @param args Command arguments (not used for this command)
 */
void MPDInterface::handleCommandListBeginCommand(const String& args) {
  inCommandList = true;
  commandListOK = false;
  commandListCount = 0;
}

/**
 * @brief Handle the MPD command_list_ok_begin command
 * @details This function is called when the command_list_ok_begin command is received.
 * It puts the MPD interface into command list mode where each command in the list
 * will receive a "list_OK" response instead of the standard "OK" response.
 * 
 * Command list mode allows clients to send multiple commands as a single atomic
 * operation. In command_list_ok_begin mode, each command (except the final 
 * command_list_end) receives a "list_OK" response, allowing the client to know
 * that each individual command was processed successfully.
 * 
 * The function sets the following state variables:
 * - inCommandList: true to indicate command list mode is active
 * - commandListOK: true to indicate list_OK responses should be used
 * - commandListCount: 0 to reset the command counter
 * 
 * The actual command list processing is handled by the handleCommandList function
 * when command_list_end is received.
 * 
 * @param args Command arguments (not used for this command)
 */
void MPDInterface::handleCommandListOkBeginCommand(const String& args) {
  inCommandList = true;
  commandListOK = true;
  commandListCount = 0;
}

/**
 * @brief Handle the MPD command_list_end command
 * @details This function is called when the command_list_end command is received.
 * It should only be called when in command list mode (after command_list_begin 
 * or command_list_ok_begin). If called outside of command list mode, it sends
 * an error response indicating that the command is not valid in the current context.
 * 
 * This function validates that we're in command list mode and then executes
 * all buffered commands, sending appropriate responses (OK or list_OK) based 
 * on the command list type, then resets command list state.
 * 
 * @param args Command arguments (not used for this command)
 */
void MPDInterface::handleCommandListEndCommand(const String& args) {
  if (inCommandList) {
    // Execute all buffered commands
    for (int i = 0; i < commandListCount; i++) {
      // Yield to allow other tasks to run
      yield();
      handleMPDCommand(commandList[i]);
    }
    // Reset command list state
    inCommandList = false;
    commandListOK = false;
    commandListCount = 0;
    mpdClient.print(mpdResponseOK());
  } else {
    mpdClient.print(mpdResponseError("command_list", "Not in command list mode"));
  }
}

/**
 * @brief Handle the MPD decoders command
 * @details This function responds to the MPD "decoders" command by returning a list
 * of supported audio decoders and their capabilities. The ESP32-audioI2S library
 * supports multiple audio formats which are reported through this command.
 * 
 * For each supported decoder, the function reports:
 * - Plugin name (identifier for the decoder)
 * - Supported file suffixes/extensions
 * - MIME types for the supported formats
 * 
 * This information allows MPD clients to understand what audio formats
 * the CubeRadio can decode and play.
 * 
 * Currently supported formats:
 * - MP3: Using the HelixMP3 decoder
 * - AAC: Using the HelixAAC decoder
 * 
 * @param args Command arguments (not used for this command)
 */
void MPDInterface::handleDecodersCommand(const String& args) {
  mpdClient.print("plugin: HelixMP3\n");
  mpdClient.print("suffix: mp3\n");
  mpdClient.print("mime_type: audio/mpeg\n");
  mpdClient.print("plugin: HelixAAC\n");
  mpdClient.print("suffix: aac\n");
  mpdClient.print("mime_type: audio/aac\n");
  mpdClient.print(mpdResponseOK());
}

/**
 * @brief Constructor for MPDInterface
 * @details Initializes the MPD interface with references to global variables
 * and the WiFi server instance. All parameters are passed by reference to
 * maintain direct access to the global state.
 * 
 * The constructor initializes all member variables with the provided references
 * and sets initial state for command processing.
 * 
 * @param serverRef WiFiServer instance for MPD connections
 * @param playerRef Player instance for controlling playback
 */
MPDInterface::MPDInterface(WiFiServer& serverRef, Player& playerRef)
    : mpdServer(serverRef), player(playerRef) {
  // Initialize supported commands list
  supportedCommands = {
    "add", "clear", "close", "currentsong", "delete", "disableoutput", 
    "enableoutput", "find", "idle", "kill", "list", "listallinfo", 
    "listplaylistinfo", "listplaylists", "load", "lsinfo", "next", 
    "notcommands", "outputs", "password", "pause", "ping", "play", "playid", 
    "playlistid", "playlistinfo", "plchanges", "previous", "save", "search", 
    "seek", "seekid", "setvol", "stats", "status", "stop", "tagtypes", 
    "update"
  };
  // Default tagtypes response
  supportedTagTypes = {
    "Artist", "Album", "Title", "Track", "Name", "Genre", "Date", "Comment", "Disc"
  };
}

/**
 * @brief Command registry mapping MPD commands to their handlers
 * @details This static array defines all supported MPD commands and their corresponding
 * handler functions. Each entry contains:
 * - Command name as a string literal
 * - Pointer to the handler function (member function pointer)
 * - Exact match flag (true = exact match, false = prefix match)
 * 
 * The registry is used by the command processing system to efficiently dispatch
 * incoming MPD commands to their appropriate handlers. Commands are matched
 * either exactly (for commands like "stop") or as prefixes (for commands like
 * "play" which should also match "playid").
 * 
 * The registry is sorted alphabetically by command name for readability, though
 * the command processing system does a linear search through all entries.
 * 
 * Adding new commands requires:
 * 1. Adding a handler function declaration in the header file
 * 2. Implementing the handler function
 * 3. Adding an entry to this registry
 * 4. Adding the command to supportedCommands vector if it should appear in "commands" response
 */
const MPDInterface::MPDCommand MPDInterface::commandRegistry[] = {
  {"stop", &MPDInterface::handleStopCommand, true},
  {"pause", &MPDInterface::handleStopCommand, false},
  {"status", &MPDInterface::handleStatusCommand, true},
  {"currentsong", &MPDInterface::handleCurrentSongCommand, true},
  {"playlistinfo", &MPDInterface::handlePlaylistInfoCommand, false},
  {"playlistid", &MPDInterface::handlePlaylistIdCommand, false},
  {"playid", &MPDInterface::handlePlayCommand, false},
  {"play", &MPDInterface::handlePlayCommand, false},
  {"lsinfo", &MPDInterface::handleLsInfoCommand, true},
  {"setvol", &MPDInterface::handleSetVolCommand, false},
  {"getvol", &MPDInterface::handleGetVolCommand, true},
  {"volume", &MPDInterface::handleVolumeCommand, false},
  {"next", &MPDInterface::handleNextCommand, true},
  {"previous", &MPDInterface::handlePreviousCommand, true},
  {"clear", &MPDInterface::handleClearCommand, true},
  {"add", &MPDInterface::handleAddCommand, true},
  {"delete", &MPDInterface::handleDeleteCommand, true},
  {"load", &MPDInterface::handleLoadCommand, true},
  {"save", &MPDInterface::handleSaveCommand, true},
  {"outputs", &MPDInterface::handleOutputsCommand, true},
  {"disableoutput", &MPDInterface::handleDisableOutputCommand, false},
  {"enableoutput", &MPDInterface::handleEnableOutputCommand, false},
  {"commands", &MPDInterface::handleCommandsCommand, true},
  {"notcommands", &MPDInterface::handleNotCommandsCommand, true},
  {"stats", &MPDInterface::handleStatsCommand, true},
  {"ping", &MPDInterface::handlePingCommand, true},
  {"password", &MPDInterface::handlePasswordCommand, false},
  {"kill", &MPDInterface::handleKillCommand, true},
  {"update", &MPDInterface::handleUpdateCommand, true},
  {"listallinfo", &MPDInterface::handleListAllInfoCommand, true},
  {"listplaylistinfo", &MPDInterface::handleListPlaylistInfoCommand, true},
  {"listplaylists", &MPDInterface::handleListPlaylistsCommand, true},
  {"list", &MPDInterface::handleListCommand, false},
  {"search", &MPDInterface::handleSearchCommand, false},
  {"find", &MPDInterface::handleFindCommand, false},
  {"seek", &MPDInterface::handleSeekCommand, false},
  {"seekid", &MPDInterface::handleSeekIdCommand, false},
  {"tagtypes", &MPDInterface::handleTagTypesCommand, false},
  {"plchanges", &MPDInterface::handlePlChangesCommand, false},
  {"idle", &MPDInterface::handleIdleCommand, true},
  {"noidle", &MPDInterface::handleNoIdleCommand, true},
  {"close", &MPDInterface::handleCloseCommand, true},
  {"command_list_begin", &MPDInterface::handleCommandListBeginCommand, true},
  {"command_list_ok_begin", &MPDInterface::handleCommandListOkBeginCommand, true},
  {"command_list_end", &MPDInterface::handleCommandListEndCommand, true},
  {"decoders", &MPDInterface::handleDecodersCommand, true}
};
// Calculate the number of commands in the registry
const size_t MPDInterface::commandCount = sizeof(commandRegistry) / sizeof(MPDCommand);

/**
 * @brief Handle MPD client connections and process commands
 * @details Manages the MPD protocol connection state and command processing.
 * 
 * This function handles new client connections, processes incoming MPD commands,
 * and manages special modes like command lists and idle mode. It also handles
 * client disconnections and ensures proper cleanup.
 * 
 * Connection handling:
 * - Accepts new connections when no client is connected
 * - Rejects new connections when a client is already connected
 * - Properly closes disconnected clients
 * 
 * In idle mode, the function monitors for changes in playback status and stream
 * information, sending notifications to clients when changes occur.
 * 
 * The function implements proper resource management:
 * - Ensures previous clients are properly closed before accepting new ones
 * - Resets command processing state on new connections
 * - Handles unexpected disconnections gracefully
 */
void MPDInterface::handleClient() {
    // Check if client disconnected unexpectedly
    if (mpdClient && !mpdClient.connected()) {
        mpdClient.flush();
        delay(1);
        mpdClient.stop();
        // Reset all state variables
        inCommandList = false;
        commandListOK = false;
        commandListCount = 0;
        inIdleMode = false;
        commandBuffer = "";
        discardingCommand = false;
        return;
    }
    // Handle new client connections
    if (mpdServer.hasClient()) {
        // Reject new connection if we already have one
        if (mpdClient && mpdClient.connected()) {
            WiFiClient newClient = mpdServer.available();
            if (newClient && newClient.connected()) {
                newClient.print("OK MPD " MPD_VERSION "\n");
                newClient.print("ACK [0@0] {} Only one client allowed at a time\n");
                newClient.flush();
                delay(1);
                newClient.stop();
            }
            return;
        }
        // Properly close existing client first
        if (mpdClient && mpdClient.connected()) {
            mpdClient.flush();
            delay(1);
            mpdClient.stop();
        }
        // Accept the new client connection
        mpdClient = mpdServer.available();
        // Send MPD welcome message with error checking
        if (mpdClient && mpdClient.connected()) {
            mpdClient.print("OK MPD " MPD_VERSION "\n");
        }
        // Reset all state variables
        inCommandList = false;
        commandListOK = false;
        commandListCount = 0;
        inIdleMode = false;
        commandBuffer = "";
        discardingCommand = false;
    }
    // Process client if connected
    if (mpdClient && mpdClient.connected()) {
        if (inIdleMode) {
            handleIdleMode();
        } else {
            handleAsyncCommands();
        }
    }
}

/**
 * @brief Handle idle mode monitoring and notifications
 * @details Monitors for changes in playback status and stream information using
 * hash-based change detection for efficient monitoring.
 * 
 * Change detection works by computing hash values of the monitored data:
 * - Title hash: Computed from the stream title string using polynomial rolling hash
 * - Status hash: Computed from playing status, volume, and bitrate
 * 
 * When changes are detected, appropriate MPD idle notifications are sent:
 * - "changed: playlist" for title changes
 * - "changed: player" and "changed: mixer" for status changes
 * 
 * The function also handles the noidle command to exit idle mode.
 * 
 * Hash computation uses a simple polynomial rolling hash with base 31 for good
 * distribution properties while being computationally efficient.
 * 
 * The idle monitoring implements an efficient polling mechanism:
 * - Computes rolling hashes of monitored data on each call
 * - Compares with previously stored hashes to detect changes
 * - Sends appropriate MPD notifications when changes occur
 * - Handles noidle command to exit idle mode gracefully
 * - Maintains low CPU usage through minimal processing per call
 */
void MPDInterface::handleIdleMode() {
  // Check for title changes using hash computation
  // Uses polynomial rolling hash with base 31 for good distribution
  unsigned long currentTitleHash = 0;
  for (int i = 0; this->player.getStreamTitle()[i]; i++) {
    currentTitleHash = currentTitleHash * 31 + this->player.getStreamTitle()[i];
  }
  // Check for status changes using hash computation
  // Combines playing status (boolean) and volume (0-22) into a single hash
  unsigned long currentStatusHash = this->player.isPlaying() ? 1 : 0;
  currentStatusHash = currentStatusHash * 31 + this->player.getVolume();
  // Prepare to send idle response if changes detected
  bool sendIdleResponse = false;
  String idleChanges = "";
  // Check for title change - indicates playlist content change
  if (currentTitleHash != lastTitleHash) {
    idleChanges += "changed: playlist\n";
    lastTitleHash = currentTitleHash;
    sendIdleResponse = true;
  }
  // Check for status change - indicates player or mixer state change
  if (currentStatusHash != lastStatusHash) {
    idleChanges += "changed: player\n";
    idleChanges += "changed: mixer\n";
    lastStatusHash = currentStatusHash;
    sendIdleResponse = true;
  }
  // Send idle response if there are changes
  if (sendIdleResponse) {
    if (mpdClient && mpdClient.connected()) {
        mpdClient.print(idleChanges);
        mpdClient.print(mpdResponseOK());
    }
    inIdleMode = false;
    return;
  }
  // Check if there's data available (for noidle command)
  if (mpdClient && mpdClient.connected() && mpdClient.available()) {
    String command = mpdClient.readStringUntil('\n');
    command.trim();
    Serial.println("MPD Command: " + command);
    // Handle noidle command to exit idle mode
    if (command == "noidle") {
      inIdleMode = false;
      if (mpdClient && mpdClient.connected()) {
          mpdClient.print(mpdResponseOK());
      }
    }
  }
}
/**
 * @brief Handle playback command
 * @details Common handler for play and playid commands to reduce code duplication.
 * This function handles the actual playback logic for both commands, including
 * starting the stream, updating state, and sending responses.
 * 
 * The function implements robust playback handling with proper validation:
 * - Validates playlist existence before attempting playback
 * - Handles both explicit index and current selection scenarios
 * - Ensures index bounds checking to prevent array access errors
 * - Updates global state variables for consistency
 * - Triggers state persistence to maintain settings across reboots
 * 
 * Playback flow:
 * 1. Validate playlist exists
 * 2. Determine target index (provided index or current selection)
 * 3. Validate index is within playlist bounds
 * 4. Update current selection
 * 5. Start stream playback
 * 6. Mark player state as dirty and save
 * 
 * @param index The playlist index to play (-1 for current selection)
 * @return true if playback started successfully, false otherwise
 */
bool MPDInterface::handlePlayback(int index) {
  if (this->player.getPlaylistCount() <= 0) {
    return false;
  }
  // Use current selection if no valid index provided
  if (index < 0 || index >= this->player.getPlaylistCount()) {
    index = this->player.getPlaylistIndex();
  }
  // Validate that we have a valid index within playlist bounds
  if (index < 0 || index >= this->player.getPlaylistCount()) {
    return false;
  }
  // Stop any current playback
  this->player.stopStream();
  // Update current selection
  this->player.setPlaylistIndex(index);
  // Start playback
  const StreamInfo& item = this->player.getPlaylistItem(index);
  this->player.startStream(item.url, item.name);
  // Update state
  this->player.savePlayerState();
  return true;
}

/**
 * @brief Handle asynchronous command processing
 * @details Processes commands without blocking, allowing for better responsiveness.
 * This function reads available data from the client connection and accumulates
 * it in a buffer until a complete command (terminated by newline) is received.
 * It then processes the command according to the current mode (normal, command list, etc.).
 * 
 * The function processes only one command per call to avoid blocking other operations,
 * which is important for maintaining responsive MPD service.
 * 
 * Command processing implements a state machine with the following states:
 * - Normal mode: Process commands immediately
 * - Command list mode: Buffer commands until command_list_end
 * - Idle mode: Handled separately in handleIdleMode()
 * 
 * The function implements proper buffering and command boundary detection:
 * - Accumulates characters until newline delimiter is found
 * - Trims whitespace from commands
 * - Routes commands to appropriate handlers based on current mode
 * - Processes one command at a time to maintain responsiveness
 */
void MPDInterface::handleAsyncCommands() {
    // Read available data without blocking
    while (mpdClient.available()) {
        char c = mpdClient.read();
        if (c == '\n') {
            // An oversized command was being discarded; resume normally
            if (discardingCommand) {
                discardingCommand = false;
                commandBuffer = "";
                continue;
            }
            // Process complete command
            String command = commandBuffer;
            command.trim();
            commandBuffer = "";
            // Only process if command is not empty
            if (command.length() > 0) {
                Serial.println("MPD Command: " + command);
                // Handle command list mode
                if (inCommandList) {
                    handleCommandList(command);
                } else {
                    // Normal command processing
                    handleMPDCommand(command);
                }
            }
            break; // Process one command at a time to avoid blocking
        } else if (!discardingCommand) {
            // Cap the buffer so a client sending data without newlines
            // cannot exhaust the heap; discard the rest of the line
            if (commandBuffer.length() >= 512) {
                commandBuffer = "";
                discardingCommand = true;
                mpdClient.print(mpdResponseError("", "Command too long"));
            } else {
                commandBuffer += c;
            }
        }
    }
}

/**
 * @brief Handle command list processing
 * @details Processes commands in command list mode with support for both
 * command_list_begin and command_list_ok_begin modes.
 * 
 * In command_list_begin mode, commands are buffered until command_list_end
 * is received, then all commands are executed sequentially.
 * 
 * In command_list_ok_begin mode, each command receives a "list_OK" response
 * except the last which receives a standard "OK" response.
 * 
 * The function implements a safety limit of 50 commands to prevent memory issues
 * and potential denial of service attacks.
 * 
 * Command list processing follows the MPD protocol specification:
 * - Commands are executed in order
 * - Each command is processed as if sent individually
 * - Error in any command stops processing and returns error
 * 
 * The function implements proper command list state management:
 * - Buffers commands until command_list_end is received
 * - Executes all buffered commands sequentially
 * - Handles both command_list_begin and command_list_ok_begin modes
 * - Implements safety limit to prevent memory exhaustion
 * - Properly resets state after command list execution
 * 
 * @param command The command to process
 */
void MPDInterface::handleCommandList(const String& command) {
  if (command == "command_list_end") {
    // Execute all buffered commands
    for (int i = 0; i < commandListCount; i++) {
      // Yield to allow other tasks to run
      yield();
      handleMPDCommand(commandList[i]);
    }
    // Reset command list state
    inCommandList = false;
    commandListOK = false;
    commandListCount = 0;
    mpdClient.print(mpdResponseOK());
  } else {
    // Buffer the command
    if (commandListCount < MAX_COMMAND_LIST_SIZE) {
      commandList[commandListCount] = command;
      commandListCount++;
    } else {
      // Command list too long, send error
      inCommandList = false;
      commandListOK = false;
      commandListCount = 0;
      mpdClient.print(mpdResponseError("command_list", "Command list too long"));
    }
  }
}

/**
 * @brief Generate MPD OK response
 * @details Generates the appropriate OK response based on the current mode:
 * - In normal mode: returns "OK\n"
 * - In command list mode with list_OK enabled: returns "list_OK\n" for intermediate responses
 * - In command list mode with list_OK disabled: returns empty string for intermediate responses
 * 
 * The function follows MPD protocol specification for response formatting:
 * - Normal responses end with "\n"
 * - Command list responses follow specific sequencing rules
 * - Proper handling of intermediate vs final responses in command lists
 * 
 * @return OK response string
 */
String MPDInterface::mpdResponseOK() {
  // Check if in command list mode
  if (inCommandList) {
    // Send OK for each command if in command_list_ok_begin mode
    if (commandListOK) {
      return "list_OK\n";
    }
    else
      return "";
  }
  // Standard OK response
  return "OK\n";
}

/**
 * @brief Generate MPD error response
 * @details Generates a properly formatted MPD error response following the
 * MPD protocol specification: ACK [error_code@command_list_num] {current_command} message
 * 
 * This implementation uses appropriate error codes based on the error type:
 * - Error code 5 (ACK_ERROR_NO_EXIST) for "No such song" or resource not found
 * - Error code 2 (ACK_ERROR_ARG) for argument errors
 * - Error code 1 (ACK_ERROR_NOT_LIST) for command list errors
 * - Error code 0 (ACK_ERROR_UNKNOWN) for unknown errors
 * 
 * Command list number 0 indicates error in current command.
 * 
 * Error response format follows MPD specification:
 * - Appropriate error codes for different error types
 * - Command list number indicating where error occurred
 * - Current command name is included in braces
 * - Human-readable message follows the command name
 * 
 * @param command The command that caused the error
 * @param message Error message
 * @return Error response string in MPD format
 */
String MPDInterface::mpdResponseError(const String& command, const String& message) {
  // Determine appropriate error code based on message content
  int errorCode = 5; // Default to ACK_ERROR_NO_EXIST
  // Map common error conditions to appropriate MPD error codes
  if (message.indexOf("argument") != -1 || message.indexOf("missing") != -1 || message.indexOf("range") != -1) {
    errorCode = 2; // ACK_ERROR_ARG
  } else if (message.indexOf("command list") != -1) {
    errorCode = 1; // ACK_ERROR_NOT_LIST
  } else if (message.indexOf("unknown") != -1) {
    errorCode = 0; // ACK_ERROR_UNKNOWN
  }
  // Format the error response according to MPD specification
  return "ACK [" + String(errorCode) + "@0] {" + command + "} " + message + "\n";
}

/**
 * @brief Send playlist information with configurable detail level
 * @details Sends playlist information with different levels of metadata based
 * on the detail level parameter. Higher detail levels include more metadata fields.
 * 
 * Detail levels:
 * - 0: Minimal (file+title) - for listplaylistinfo
 * - 1: Simple (file+title+lastmod) - for lsinfo, listallinfo
 * - 2: Full (file+title+id+pos+lastmod) - for playlistinfo
 * - 3: Artist/Album (file+title+artist+album+id+pos+lastmod) - for search/find with artist/album
 * 
 * For artist/album detail levels, dummy "WebRadio" values are used since
 * web radio streams don't have traditional artist/album metadata.
 * 
 * Metadata fields included per detail level:
 * - Level 0: file, Title
 * - Level 1: file, Title, Track, Last-Modified
 * - Level 2: file, Title, Id, Pos, Track, Last-Modified
 * - Level 3: file, Title, Artist, Album, Id, Pos, Track, Last-Modified
 * 
 * @param detailLevel 
 * 0=minimal (file+title), 
 * 1=simple (file+title+lastmod), 
 * 2=full (file+title+id+pos+lastmod), 
 * 3=artist/album (file+title+artist+album+id+pos+lastmod)
 */
void MPDInterface::sendPlaylistInfo(int detailLevel) {
  for (int i = 0; i < min(this->player.getPlaylistCount(), MAX_PLAYLIST_SIZE); i++) {
    const StreamInfo& item = this->player.getPlaylistItem(i);
    mpdClient.print("file: " + String(item.url) + "\n");
    mpdClient.print("Title: " + String(item.name) + "\n");
    if (detailLevel >= 3) {
      // Artist/Album detail level
      mpdClient.print("Artist: WebRadio\n");
      mpdClient.print("Album: WebRadio\n");
    }
    if (detailLevel >= 2) {
      // Full detail level
      mpdClient.print("Id: " + String(i) + "\n");
      mpdClient.print("Pos: " + String(i) + "\n");
    }
    if (detailLevel >= 1) {
      // Simple detail level
      mpdClient.print("Track: " + String(i + 1) + "\n");
      mpdClient.print("Last-Modified: " + String(BUILD_TIME) + "\n");
    }
  }
}

/**
 * @brief Handle MPD search/find commands
 * @details Processes search and find commands with partial or exact matching in stream names.
 * The function parses the search criteria and filters the playlist entries based on
 * the specified matching mode (partial for search, exact for find).
 * 
 * Command format examples:
 * - search "Title" "search term"
 * - find "Artist" "exact artist name"
 * 
 * Special handling is implemented for artist/album searches which return
 * simple playlist information rather than filtered results.
 * 
 * Search is case-insensitive and handles quoted strings properly.
 * 
 * The function implements proper parsing of search commands:
 * - Extracts search filter and term from command string
 * - Handles quoted strings with proper quote removal
 * - Performs case-insensitive comparison
 * - Supports both partial (search) and exact (find) matching
 * 
 * Search algorithm implements efficient string matching:
 * - Case-insensitive comparison by converting to lowercase
 * - Exact matching for 'find' commands (strcmp equivalent)
 * - Partial matching for 'search' commands (strstr equivalent)
 * - Special handling for artist/album tags (returns all entries)
 * - Proper metadata formatting for matched entries
 * 
 * @param command The full command string
 * @param exactMatch Whether to perform exact matching (find) or partial matching (search)
 */
void MPDInterface::handleMPDSearchCommand(const String& args, bool exactMatch) {
  // Validate arguments length
  if (args.length() > 200) { // Reasonable limit for search arguments
    mpdClient.print(mpdResponseError("search/find", "Arguments too long"));
    return;
  }
  // Extract search filter and term
  String searchFilter = args;
  searchFilter.trim();
  String searchTerm;
  // Validate that we have arguments
  if (searchFilter.length() == 0) {
    mpdClient.print(mpdResponseError("search/find", "Missing search arguments"));
    return;
  }
  // Extract search filter (after the first space and before the next space)
  int firstSpace = searchFilter.indexOf(' ');
  if (firstSpace != -1) {
    // Extract search term (everything after the next space)
    searchTerm = searchFilter.substring(firstSpace + 1);
    searchTerm.trim();
    // Extract search filter (everything before the next space)
    searchFilter = searchFilter.substring(0, firstSpace);
    searchFilter.trim();
  }
  // Validate search filter and term
  if (searchFilter.length() == 0) {
    mpdClient.print(mpdResponseError("search/find", "Missing search filter"));
    return;
  }
  // Check for search term
  if (searchTerm.length() == 0) {
    mpdClient.print(mpdResponseError("search/find", "Missing search term"));
    return;
  }
  // Validate search filter type
  String lowerFilter = searchFilter;
  lowerFilter.toLowerCase();
  if (lowerFilter != "title" && lowerFilter != "artist" && lowerFilter != "album") {
    mpdClient.print(mpdResponseError("search/find", "Unsupported search filter"));
    return;
  }
  // Remove quotes if present
  if (searchFilter.startsWith("\"") && searchFilter.endsWith("\"") && searchFilter.length() >= 2) {
    searchFilter = searchFilter.substring(1, searchFilter.length() - 1);
  }
  if (searchTerm.startsWith("\"") && searchTerm.endsWith("\"") && searchTerm.length() >= 2) {
    searchTerm = searchTerm.substring(1, searchTerm.length() - 1);
  }
  // Validate search term length
  if (searchTerm.length() == 0) {
    mpdClient.print(mpdResponseError("search/find", "Empty search term"));
    return;
  }
  // Handle special cases for artist/album searches
  if (lowerFilter == "album" || lowerFilter == "artist") {
    sendPlaylistInfo(1); // Send simple info for album search
    return;
  }
  // Search in playlist names
  for (int i = 0; i < this->player.getPlaylistCount(); i++) {
    const StreamInfo& item = this->player.getPlaylistItem(i);
    String playlistName = String(item.name);
    // Validate playlist name
    if (playlistName.length() == 0) {
      continue;
    }
    // Convert both to lowercase for case-insensitive comparison
    String lowerName = playlistName;
    lowerName.toLowerCase();
    String lowerSearch = searchTerm;
    lowerSearch.toLowerCase();
    // Determine if there's a match based on mode
    bool match = false;
    if (exactMatch) {
      // Exact match for find command
      match = (lowerName == lowerSearch);
    } else {
      // Partial match for search command
      match = (lowerName.indexOf(lowerSearch) != -1);
    }
    // If a match is found, send the metadata
    if (match) {
      mpdClient.print("file: " + String(item.url) + "\n");
      mpdClient.print("Title: " + String(item.name) + "\n");
      mpdClient.print("Track: " + String(i + 1) + "\n");
      mpdClient.print("Last-Modified: " + String(BUILD_TIME) + "\n");
    }
    yield(); // Allow other tasks to run
  }
}

/**
 * @brief Handle MPD commands
 * @details Processes MPD protocol commands with support for MPD protocol version MPD_VERSION.
 * This function serves as the entry point for command processing, delegating to the
 * registry-based command execution system for efficient dispatch.
 * 
 * The function implements the MPD protocol command processing flow:
 * - Delegates to executeCommand() for registry-based lookup and execution
 * - Maintains compatibility with existing command handling patterns
 * - Ensures proper error handling and response formatting
 * 
 * Command processing follows the MPD protocol specification:
 * - Commands are processed in the order received
 * - Each command receives an appropriate response (OK, list_OK, or error)
 * - State changes are properly tracked and notified to clients
 * 
 * @param command The command string to process
 */
void MPDInterface::handleMPDCommand(const String& command) {
  executeCommand(command);
}

/**
 * @brief Execute command using registry lookup
 * @details Finds and executes the appropriate handler for a given command using
 * a registry-based approach for better organization and maintainability. Commands 
 * are mapped to handler functions using a lookup table for efficient command dispatch.
 * 
 * The function implements command matching with support for both exact and prefix matching:
 * - Exact matching for commands like "stop", "status", etc.
 * - Prefix matching for commands like "play" that need to handle "playid" as well
 * 
 * Command argument extraction is handled automatically based on the matching type:
 * - For exact matches, all remaining text is treated as arguments
 * - For prefix matches, text after the command name (plus space) is treated as arguments
 * 
 * The function implements proper command dispatch following MPD protocol requirements:
 * - Efficient O(n) lookup where n is the number of registered commands
 * - Proper argument parsing and passing to handlers
 * - Standardized error handling for unknown commands
 * - Special handling for empty commands (returns OK)
 * 
 * @param command The command string to execute
 * @return true if command was found and executed, false otherwise
 */
bool MPDInterface::executeCommand(const String& command) {
  // Validate command string
  if (command.length() == 0) {
    mpdClient.print(mpdResponseOK());
    return true;
  }
  // Search for matching command in registry
  for (size_t i = 0; i < commandCount; i++) {
    // Yield to allow other tasks to run
    yield();
    const MPDCommand& cmd = commandRegistry[i];
    // Check for exact or prefix match
    if (cmd.exactMatch) {
      if (command.equals(cmd.name)) {
        (this->*cmd.handler)("");
        return true;
      }
    } else {
      if (command.startsWith(cmd.name)) {
        // Extract arguments (everything after the command name)
        String args = "";
        if (command.length() > strlen(cmd.name)) {
          // +1 for space
          args = command.substring(strlen(cmd.name) + 1);
        }
        (this->*cmd.handler)(args);
        return true;
      }
    }
  }
  // Unknown command
  mpdClient.print(mpdResponseError(command, "Unknown command"));
  return false;
}
