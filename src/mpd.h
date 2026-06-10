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

#ifndef MPD_H
#define MPD_H

#include <Arduino.h>
#include <WiFi.h>
#include <Audio.h>
#include <vector>
#include <string>

// Forward declarations
#include "playlist.h"
class Player;

// External global variables
extern unsigned long totalPlayTime;
extern unsigned long playStartTime;

// External function declarations
extern void markPlayerStateDirty();
extern void savePlayerState();

// Forward declaration for argument parser
class MPDArgumentParser;

/**
 * @brief MPD Interface Class
 * @details Encapsulates all MPD protocol functionality for the CubeRadio.
 * This class handles MPD client connections, command processing, and protocol
 * compliance for controlling the internet radio player.
 * 
 * The MPD interface implements a subset of the MPD protocol (version 0.23.0)
 * to allow control via standard MPD clients. It supports:
 * - Playback control (play, stop, pause, next, previous)
 * - Volume control (setvol, getvol, volume)
 * - Playlist management (playlistinfo, lsinfo, etc.)
 * - Status queries (status, currentsong, stats)
 * - Search functionality (search, find)
 * - System commands (ping, commands, tagtypes)
 * - Special modes (idle, command lists)
 */
class MPDInterface {
private:
  WiFiServer& mpdServer;             ///< WiFi server instance for MPD connections
  WiFiClient mpdClient;              ///< Current MPD client connection

  // Reference to player instance
  Player& player;                    ///< Reference to Player instance

  // MPD command list state variables for batch command processing
  static const int MAX_COMMAND_LIST_SIZE = 20;  ///< Maximum number of commands in a command list
  String commandList[MAX_COMMAND_LIST_SIZE];    ///< Buffer for command list
  bool inCommandList = false;        ///< Flag indicating if we're in command list mode
  bool commandListOK = false;        ///< Flag indicating if we should send list_OK responses
  int commandListCount = 0;          ///< Number of commands in the current command list
  bool commandFailed = false;        ///< Set when a command generates an error response

  // MPD idle state variables for efficient change notification
  bool inIdleMode = false;           ///< Flag indicating if we're in idle mode
  unsigned long lastTitleHash = 0;   ///< Hash of last stream title for change detection
  unsigned long lastStatusHash = 0;  ///< Hash of last status for change detection

  // Asynchronous command handling buffer for non-blocking processing
  String commandBuffer = "";         ///< Buffer for accumulating incoming commands
  bool discardingCommand = false;    ///< Discarding the rest of an oversized command line
  
  // Supported MPD commands list
  std::vector<std::string> supportedCommands;
  // Supported MPD tag types
  std::vector<std::string> supportedTagTypes;

  // Command registry structure
  struct MPDCommand {
    const char* name;
    void (MPDInterface::*handler)(const String& args);
    bool exactMatch;  // true = exact match, false = prefix match
  };
  
  // Command registry
  static const MPDCommand commandRegistry[];
  static const size_t commandCount;


public:
  MPDInterface(WiFiServer& serverRef, Player& playerRef);

  /**
   * @brief Handle MPD client connections and process commands
   * @details Manages the MPD protocol connection state and command processing.
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
   */
  void handleClient();

private:
  /**
   * @brief Handle asynchronous command processing
   * @details Processes commands without blocking, allowing for better responsiveness.
   * This function reads available data from the client connection and accumulates
   * it in a buffer until a complete command (terminated by newline) is received.
   * It then processes the command according to the current mode (normal, command list, etc.).
   * 
   * The function processes only one command per call to avoid blocking other operations,
   * which is important for maintaining responsive MPD service.
   */
  void handleAsyncCommands();

  /**
   * @brief Handle playback command
   * @details Common handler for play and playid commands to reduce code duplication.
   * This function handles the actual playback logic for both commands, including
   * starting the stream, updating state, and sending responses.
   * 
   * @param index The playlist index to play (-1 for current selection)
   * @return true if playback started successfully, false otherwise
   */
  bool handlePlayback(int index = -1);

  /**
   * @brief Handle idle mode monitoring and notifications
   * @details Monitors for changes in playback status and stream information using
   * hash-based change detection for efficient monitoring.
   * 
   * Change detection works by computing hash values of the monitored data:
   * - Title hash: Computed from the stream title string
   * - Status hash: Computed from playing status, volume, and bitrate
   * 
   * When changes are detected, appropriate MPD idle notifications are sent:
   * - "changed: playlist" for title changes
   * - "changed: player" and "changed: mixer" for status changes
   * 
   * The function also handles the noidle command to exit idle mode.
   */
  void handleIdleMode();

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
   * The function implements a safety limit of 50 commands to prevent memory issues.
   * @param command The command to process
   */
  void handleCommandList(const String& command);

  /**
   * @brief Generate MPD OK response
   * @details Generates the appropriate OK response based on the current mode:
   * - In normal mode: returns "OK\n"
   * - In command list mode with list_OK enabled: returns "list_OK\n" for intermediate responses
   * - In command list mode with list_OK disabled: returns empty string for intermediate responses
   * @return OK response string
   */
  String mpdResponseOK();

  /**
   * @brief Generate MPD error response
   * @details Generates a properly formatted MPD error response following the
   * MPD protocol specification: ACK [error_code@command_list_num] {current_command} message
   * 
   * This implementation uses error code 5 (ACK_ERROR_NO_EXIST) and command list
   * number 0 as these are appropriate for most general errors.
   * @param command The command that caused the error
   * @param message Error message
   * @return Error response string in MPD format
   */
  String mpdResponseError(const String& command, const String& message);

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
   * @param detailLevel 
   * 0=minimal (file+title), 
   * 1=simple (file+title+lastmod), 
   * 2=full (file+title+id+pos+lastmod), 
   * 3=artist/album (file+title+artist+album+id+pos+lastmod)
   */
  void sendPlaylistInfo(int detailLevel);

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
   * @param command The full command string
   * @param exactMatch Whether to perform exact matching (find) or partial matching (search)
   */
  void handleMPDSearchCommand(const String& command, bool exactMatch);

  /**
   * @brief Handle MPD commands
   * @details Processes MPD protocol commands with support for MPD protocol version 0.23.0.
   * This function processes MPD protocol commands and controls the player accordingly.
   * It supports a subset of MPD commands including playback control, volume control,
   * playlist management, status queries, and search functionality.
   * 
   * Command processing includes:
   * - Playback control (play, stop, pause, next, previous)
   * - Volume control (setvol, getvol, volume)
   * - Status queries (status, currentsong, stats)
   * - Playlist management (playlistinfo, playlistid, lsinfo, listallinfo, listplaylistinfo)
   * - Search functionality (search, find)
   * - System commands (ping, commands, notcommands, tagtypes, outputs)
   * - Special modes (idle, noidle, command lists)
   * 
   * Volume handling converts between MPD's 0-100 scale and the ESP32-audioI2S 0-22 scale.
   * @param command The command string to process
   * 
   * Supported commands include:
   * - Playback: play, stop, pause, next, previous
   * - Volume: setvol, getvol, volume
   * - Status: status, currentsong, stats
   * - Playlist: playlistinfo, playlistid, lsinfo, listallinfo, listplaylistinfo
   * - Search: search, find
   * - System: ping, commands, notcommands, tagtypes, outputs
   * - Special modes: idle, noidle, command lists
   */
  void handleMPDCommand(const String& command);
  
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
   * @param command The command string to execute
   * @return true if command was found and executed, false otherwise
   */
  bool executeCommand(const String& command);

  // Individual command handlers
  void handleStopCommand(const String& args);
  void handleStatusCommand(const String& args);
  void handleCurrentSongCommand(const String& args);
  void handlePlaylistInfoCommand(const String& args);
  void handlePlaylistIdCommand(const String& args);
  void handlePlayCommand(const String& args);
  void handleLsInfoCommand(const String& args);
  void handleSetVolCommand(const String& args);
  void handleGetVolCommand(const String& args);
  void handleVolumeCommand(const String& args);
  void handleNextCommand(const String& args);
  void handlePreviousCommand(const String& args);
  void handleClearCommand(const String& args);
  void handleAddCommand(const String& args);
  void handleDeleteCommand(const String& args);
  void handleLoadCommand(const String& args);
  void handleSaveCommand(const String& args);
  void handleOutputsCommand(const String& args);
  void handleDisableOutputCommand(const String& args);
  void handleEnableOutputCommand(const String& args);
  void handleCommandsCommand(const String& args);
  void handleNotCommandsCommand(const String& args);
  void handleStatsCommand(const String& args);
  void handlePingCommand(const String& args);
  void handlePasswordCommand(const String& args);
  void handleKillCommand(const String& args);
  void handleUpdateCommand(const String& args);
  void handleListAllInfoCommand(const String& args);
  void handleListPlaylistInfoCommand(const String& args);
  void handleListPlaylistsCommand(const String& args);
  void handleListCommand(const String& args);
  void handleSearchCommand(const String& args);
  void handleFindCommand(const String& args);
  void handleSeekCommand(const String& args);
  void handleSeekIdCommand(const String& args);
  void handleTagTypesCommand(const String& args);
  void handlePlChangesCommand(const String& args);
  void handleIdleCommand(const String& args);
  void handleNoIdleCommand(const String& args);
  void handleCloseCommand(const String& args);
  void handleCommandListBeginCommand(const String& args);
  void handleCommandListOkBeginCommand(const String& args);
  void handleCommandListEndCommand(const String& args);
  void handleDecodersCommand(const String& args);
};

#endif // MPD_H
