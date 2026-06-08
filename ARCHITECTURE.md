# CubeRadio — Architecture

## Block Diagram

```
┌──────────────────────────────────────────────────────────────────────┐
│  HARDWARE INPUTS                         HARDWARE OUTPUTS            │
│  ┌────────────┐  ┌──────────────┐        ┌──────────┐  ┌──────────┐ │
│  │  Rotary    │  │  Touch btns  │        │   OLED   │  │  I2S DAC │ │
│  │  Encoder   │  │  (optional)  │        │  Display │  │ /Amp+Spk │ │
│  └─────┬──────┘  └──────┬───────┘        └────┬─────┘  └────┬─────┘ │
│        │ ISR             │ ISR/poll            │              │       │
└────────┼─────────────────┼─────────────────────┼──────────────┼───────┘
         │                 │                     │              │
┌────────▼─────────────────▼─────────────────────▼──────────────▼───────┐
│                        MAIN LOOP  (core 1, ~150 ms tick)               │
│                                                                         │
│  handleRotary()   handleTouch()   updateDisplay()   Audio watchdog      │
│  handleBoardButton()   OTA.handle()   WiFi reconnect (60 s)             │
│                                                                         │
│   ┌──────────────┐   ┌──────────────┐   ┌──────────────────────────┐  │
│   │  WebServer   │   │  WebSocket   │   │     MPDInterface         │  │
│   │  port 80     │   │  port 81     │   │     port 6600            │  │
│   │  REST API    │   │  push status │   │     MPD 0.23.0           │  │
│   └──────┬───────┘   └──────┬───────┘   └──────────┬───────────────┘  │
└──────────┼──────────────────┼──────────────────────┼────────────────────┘
           │                  │                      │
           └──────────────────┴──────────────────────┘
                              │   all control paths converge
                    ┌─────────▼──────────┐
                    │      Player        │  player.cpp / player.h
                    │                   │
                    │  PlayerState       │  playing, volume, tone,
                    │  StreamInfoData    │  url, name, title, bitrate
                    │  Playlist*         │  index, count
                    │                   │
                    │  startStream()     │
                    │  stopStream()      │
                    │  setVolume()       │
                    │  setTone()         │
                    └─────────┬──────────┘
                              │
                    ┌─────────▼──────────┐
                    │  ESP32-audioI2S    │  FreeRTOS task, core 0
                    │  (Audio library)   │  priority 5, 1 ms cycle
                    │                   │
                    │  connecttohost()   │  HTTP/S → TCP stream
                    │  loop()           │  decode MP3/AAC/FLAC
                    │  setVolume()       │  → I2S pins
                    │  setTone()         │
                    └─────────┬──────────┘
                              │
                    ┌─────────▼──────────┐
                    │  Audio callbacks   │  called in main context
                    │  (main.cpp)        │
                    │  showstreamtitle   │→ player.setStreamTitle()
                    │  showstation       │→ player.setStreamName()
                    │  bitrate           │→ player.setBitrate()
                    │  icyurl / info     │→ ICY / cover art
                    └────────────────────┘

                    ┌────────────────────┐
                    │  Playlist          │  playlist.cpp / playlist.h
                    │                   │
                    │  StreamInfo[20]    │  {name[96], url[128]}
                    │  load() / save()   │  ← /playlist.json (SPIFFS)
                    │  validate()        │
                    └────────────────────┘

                    ┌────────────────────┐
                    │  SPIFFS storage    │
                    │                   │
                    │  /config.json      │  Config struct ↔ web UI
                    │  /wifi.json        │  SSID/pass, max 5
                    │  /playlist.json    │  stations, max 20
                    │  /player.json      │  PlayerState persistence
                    │  /player.html …    │  static web assets
                    └────────────────────┘
```

---

## Blocks

### 1. `main.cpp` — Orchestrator

The firmware entry point and glue layer. Owns all global instances, wires
everything together in `setup()`, and drives the cooperative main loop.

**Responsibilities:**
- Initialise SPIFFS, WiFi (STA + AP), mDNS, OTA, servers, hardware
- Register HTTP routes and WebSocket event handler
- Implement all HTTP API handlers (`handleGetStreams`, `handlePostConfig`, …)
- Implement audio callbacks (`audio_showstreamtitle`, `audio_bitrate`, …)
- Run the main loop: poll servers, poll controls, update display, watch audio
- Broadcast status JSON over WebSocket (`sendStatusToClients`, `generateStatusJSON`)
- SPIFFS JSON helpers: `readJsonFile` / `writeJsonFile` (backup + rollback)

**Key globals:** `server`, `webSocket`, `mpdServer`, `player`, `mpdInterface`,
`display`, `rotaryEncoder`, `touchPlay/Next/Prev`, `config`, `ssid[]`, `password[]`

---

### 2. `Player` — Audio Player State Machine

Central domain object. Owns the `Audio*` instance, `Playlist*`, `PlayerState`,
and `StreamInfoData`. All control paths (web, MPD, physical) call into `Player`.

**Responsibilities:**
- Lifecycle: `setupAudioOutput()`, `startStream()`, `stopStream()`
- State: getters/setters for playing, volume, tone, playlist index, stream info
- Persistence: `loadPlayerState()` / `savePlayerState()` with dirty-flag batching
- Playlist delegation: `loadPlaylist()`, `savePlaylist()`, `addPlaylistItem()`, …
- Audio tick: `handleAudio()` → `audio->loop()` (called from FreeRTOS task)
- Bitrate polling: `updateBitrate()` reads live value from Audio object
- Thread safety: `portMUX_TYPE spinlock` guards shared state between core 0 (audio task) and core 1 (main loop)

**Volume scale:** internal 0–22 (ESP32-audioI2S), converted at MPD boundary to 0–100.

---

### 3. `Playlist` — Station List

Simple fixed-capacity array of `StreamInfo {name[96], url[128]}`, max 20 items.

**Responsibilities:**
- Load from / save to `/playlist.json` via ArduinoJson
- `validate()` — clamp `current` index, remove entries with empty URL
- Add, remove, set, clear individual items
- Track `current` index (kept in sync with `PlayerState.playlistIndex`)

---

### 4. `MPDInterface` — MPD Protocol Server

Full MPD 0.23.0 server on TCP port 6600, single-client, non-blocking.

**Responsibilities:**
- Accept/reject connections (`WiFiServer`); only one client at a time
- Non-blocking read: accumulate bytes into `commandBuffer`, process on `\n`
- Dispatch via `commandRegistry[]` (function-pointer table, exact + prefix match)
- Handle special modes:
  - **Command list** (`command_list_begin` / `command_list_ok_begin`): buffer up to 20 cmds, execute at `command_list_end`; safety cap 50
  - **Idle mode** (`idle`): hash-based polling for title/status changes; emit `changed:` lines
- Volume conversion: MPD 0–100 ↔ Player 0–22
- All playback actions delegate to `Player`

**Command categories:** playback, volume, status, playlist, search, system, idle, command lists.

---

### 5. `Display` — OLED Abstraction

Wraps `Adafruit_SSD1306` with three display-type layouts (128×64, 128×32, 128×32s).

**Responsibilities:**
- `begin()` — init I2C, splash screen
- `update()` — render station name, track title (with horizontal scroll), volume, bitrate, IP
- `showLogo()` / `showStatus()` — startup and informational screens
- `printAt()` — auto font selection (Spleen 6×12 / 8×16 / 16×32) based on vertical space and alignment (l/c/r)
- `handleTimeout()` — turns display off after `config.display_timeout` seconds of inactivity when stopped; keeps on while playing
- Dirty flag: only re-renders when content has changed

**Fonts:** three embedded Spleen bitmap fonts in `Spleen6x12.h`, `Spleen8x16.h`, `Spleen16x32.h`.

---

### 6. `RotaryEncoder` — Physical Input

ISR-driven quadrature decoder for volume and playlist navigation.

**Responsibilities:**
- `handleRotation()` ISR: CLK falling-edge detection, DT state → ±1 position counter, 100 ms debounce
- `handleButtonPress()` ISR: falling-edge on SW pin, 100 ms debounce, sets one-shot flag
- `getPosition()` / `setPosition()` — read and reset counter
- `wasButtonPressed()` — one-shot flag, auto-clears

**Used in** `handleRotary()` (main.cpp): if playing → adjust volume; if stopped → navigate playlist. Button → play/stop toggle.

---

### 7. `TouchButton` — Capacitive Input (optional)

Up to three capacitive touch buttons (play, next, prev), all optional (pin = -1 → skipped).

**Responsibilities:**
- Polling (`handle()`) or interrupt (`handleInterrupt()`) mode
- Raw capacitance read via `touchRead()`; compare against `threshold`
- Debounce: minimum `debounceTime` ms between state changes
- One-shot `wasPressed()` flag

**Used in** `handleTouch()` (main.cpp): touch play → play/stop toggle; touch next/prev → next/prev stream.

---

### 8. HTTP API + WebSocket (in `main.cpp`)

**HTTP (WebServer, port 80):**

| Endpoint | Handler | Side-effects |
|----------|---------|--------------|
| `GET /api/player` | `generateStatusJSON` | none |
| `POST /api/player` | parse action/url/index → `player.startStream()` / `stopStream()` | saves state, notifies clients |
| `GET/POST /api/mixer` | read/write volume + tone | saves state |
| `GET/POST /api/streams` | read/write playlist JSON | saves playlist |
| `GET/POST /api/config` | read/write `Config` struct | saves config, reinit hardware |
| `GET /api/config/export` | bundle all JSON files | read-only |
| `POST /api/config/import` | unbundle + write all files | rewrites all configs |
| `GET /api/wifi/scan` | `WiFi.scanNetworks()` | blocking scan |
| `POST /api/wifi/save` | write `wifi.json` | reconnects |
| `GET /api/wifi/status` | current connection info | none |
| `GET /api/proxy` | `HTTPClient` fetch | proxy for CORS |
| `GET/POST /w` | `handleSimpleWebPage()` | fallback control UI |

Static assets served from SPIFFS via `server.serveStatic()`.

**WebSocket (port 81):**
- `webSocketEvent()` handles connect/disconnect/text
- On connect: send full status JSON immediately
- Every 3 s while playing: send partial status (bitrate)
- On any state change: `sendStatusToClients()` broadcasts full JSON to all clients
- Change detection: `previousStatus` string diff prevents redundant sends

---

### 9. WiFi + Network Services

**STA mode:** connects to up to 5 stored networks in priority order (first network found in scan wins). Reconnect attempt every 60 s in main loop.

**AP mode:** always started (`CubeRadio` SSID, no password); provides fallback control access when no STA network is available.

**mDNS:** `CubeRadio.local`, advertises HTTP (port 80) and MPD (port 6600). Only on boards with PSRAM (`BOARD_HAS_PSRAM`).

**OTA:** `ArduinoOTA.handle()` polled every tick. SPIFFS unmounted during OTA flash.

---

### 10. SPIFFS Persistence

All configuration is stored as JSON files on SPIFFS.

**Read/write helpers** (`readJsonFile` / `writeJsonFile` in main.cpp):
- `readJsonFile`: opens file, bounds-checks size, deserialises with ArduinoJson
- `writeJsonFile`: copies existing file to `.bak`, serialises and writes new file, removes `.bak` on success; rolls back on failure

| File | Owner | Saved by |
|------|-------|---------|
| `/config.json` | `Config` struct | `saveConfig()` |
| `/wifi.json` | `ssid[]`/`password[]` arrays | `saveWiFiCredentials()` |
| `/playlist.json` | `Playlist` | `Playlist::save()` |
| `/player.json` | `PlayerState` | `Player::savePlayerState()` |

**Dirty-flag batching:** `PlayerState.dirty` is set on any state change; `savePlayerState()` is called explicitly after user actions (not on every loop tick) to avoid excessive SPIFFS writes.

---

## Threading Model

| Thread | Core | Priority | Tick | Owns |
|--------|------|----------|------|------|
| `loop()` (Arduino main) | 1 | default | ≥150 ms | servers, controls, display, state |
| `audioTask` (FreeRTOS) | 0 | 5 | 1 ms | `Audio::loop()` → I2S output |

`Player::spinlock` (`portMUX_TYPE`) protects shared `PlayerState` and `StreamInfoData` at get/set boundaries. Audio callbacks (`audio_show*`) are called from the audio library within the `audio->loop()` call on core 0, then dispatch to `player.set*()` which acquire the spinlock.

---

## Data Flow Summary

```
Internet HTTP stream
  └─→ WiFi TCP socket
        └─→ Audio::connecttohost() [core 0 audioTask]
              └─→ Audio::loop(): buffer → decode MP3/AAC/FLAC → I2S pins → speaker
                    └─→ callbacks → Player::set*() → WebSocket broadcast

User action (web / MPD / rotary / touch)
  └─→ Player::startStream(url, name)
        └─→ Audio::connecttohost()   [kicks off above chain]
        └─→ Player::savePlayerState()
        └─→ sendStatusToClients()    [WebSocket + HTTP]
        └─→ updateDisplay()          [OLED]
```

---

## Configuration & Pin Assignment

All hardware pins are stored in `Config` (loaded from `/config.json`) and applied at boot. The web UI at `/config` can change any pin and save — no reflash needed.

Board-specific compile-time defaults are in `pins_wroom.h`, `pins_wrover.h`, `pins_cam.h`, selected by `pins.h` based on build-flag macros set in `platformio.ini`.

---

## Key Constants & Limits

| Constant | Value | Location |
|----------|-------|----------|
| `MAX_WIFI_NETWORKS` | 5 | `main.h` |
| `MAX_PLAYLIST_SIZE` | 20 | `main.h` |
| `STREAM_NAME_SIZE` | 96 | `playlist.h` |
| `STREAM_URL_SIZE` | 128 | `playlist.h` |
| `StreamInfoData::url` | 256 | `player.h` |
| `StreamInfoData::name` | 128 | `player.h` |
| `StreamInfoData::title` | 128 | `player.h` |
| `MAX_COMMAND_LIST_SIZE` | 20 (cap 50) | `mpd.h` |
| `PLAYER_STATE_BUFFER_SIZE` | 512 B | `player.h` |
| `PLAYLIST_BUFFER_SIZE` | 4096 B | `player.h` / `main.h` |
| Main loop delay | 150 ms | `main.cpp` |
| Display update interval | 500 ms | `main.cpp` |
| WebSocket status interval | 3 s | `main.cpp` |
| WiFi reconnect interval | 60 s | `main.cpp` |
| Stream restart delay | 1 s | `main.cpp` |
| Rotary debounce | 100 ms | `rotary.cpp` |
| Volume range (internal) | 0–22 | `player.h` |
| Volume range (MPD) | 0–100 | `mpd.cpp` |
| Tone range (bass/mid/treble) | −6..+6 | `player.h` |
