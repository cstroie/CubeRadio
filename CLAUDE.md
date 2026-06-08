# CubeRadio — CLAUDE.md

## Project Overview

CubeRadio is an ESP32 internet radio player firmware. It streams internet radio via HTTP/HTTPS, decodes audio through I2S to a DAC/amplifier, and exposes three control interfaces: a browser web UI, a WebSocket feed, and a full MPD (Music Player Daemon) server. Physical controls are a rotary encoder and optional capacitive touch buttons; feedback is shown on an OLED display.

---

## Build System

**PlatformIO** (`platformio.ini`) — three environments:

| Environment | Board | Notes |
|-------------|-------|-------|
| `esp32wroom` (default) | ESP32 WROOM | No PSRAM, 8KB audio buffer |
| `esp32wrover` | ESP32 WROVER | PSRAM enabled, 1MB audio buffer |
| `esp32cam` | ESP32-CAM | Alternate pin mapping |

Partition table: `huge_app.csv` (large app partition to fit all libraries).

```bash
pio run -t upload       # Flash firmware
pio run -t uploadfs     # Upload data/ to SPIFFS
```

---

## Source File Map

```
src/
  main.cpp        Setup, HTTP/WebSocket handlers, main loop, audio callbacks (~2700 lines)
  main.h          Config struct, global forward declarations
  mpd.cpp/h       Full MPD 0.23.0 protocol server (~2200 lines)
  player.cpp/h    Audio playback abstraction over ESP32-audioI2S
  playlist.cpp/h  JSON-backed playlist (max 20 entries)
  display.cpp/h   OLED driver: font selection, scrolling, timeout
  rotary.cpp/h    ISR-based quadrature rotary encoder + button
  touch.cpp/h     Capacitive touch button handler (up to 3 buttons)
  pins.h          Pin macro dispatcher → pins_wroom/wrover/cam.h
  Spleen*.h       Embedded bitmap fonts (6×12, 8×16, 16×32)

data/             SPIFFS filesystem (upload with `pio run -t uploadfs`)
  player.html     Main player UI
  playlist.html   Playlist manager
  wifi.html       WiFi credentials setup
  config.html     Hardware pin configuration
  about.html      About / help page
  scripts.js      All client-side logic (~3400 lines, shared across pages)
  styles.css      Custom styles extending PicoCSS
  pico.min.css    PicoCSS v2 framework
  playlist.json   Default radio stations (20 entries)
  wifi.json       Stored WiFi credentials (up to 5 networks)
  cd.svg / logo.png / favicon.ico
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  MAIN LOOP (core 1, 150ms tick)         │
│ OTA · HTTP server · WebSocket · MPD · controls · display│
└───────────┬──────────────┬──────────────────────────────┘
            │              │
   ┌─────────────┐  ┌──────────────┐  ┌──────────────────┐
   │ WebServer   │  │ WebSocket    │  │ MPD Server       │
   │ port 80     │  │ port 81      │  │ port 6600        │
   │ REST API    │  │ status push  │  │ MPD 0.23.0 cmds  │
   └──────┬──────┘  └──────┬───────┘  └────────┬─────────┘
          │                │                   │
          └────────────────┴───────────────────┘
                           │
                  ┌────────────────┐
                  │  Player        │  player.cpp
                  │  startStream() │
                  │  setVolume()   │
                  │  PlayerState   │
                  └────────┬───────┘
                           │
                  ┌────────────────┐
                  │ ESP32-audioI2S │  FreeRTOS task, core 0
                  │ HTTP→decode    │  audio->loop() every 1ms
                  │ MP3/AAC/FLAC   │
                  └────────┬───────┘
                           │
                   I2S pins (BCLK, LRC, DOUT)
                           │
                      DAC / Amplifier → Speakers

Physical controls:
  Rotary encoder → volume (when playing) or stream select (stopped)
  Rotary button  → play / stop
  Touch buttons  → play / next / prev (optional)
  OLED display   → stream name, title, bitrate, scrolling text
```

---

## Data Flow

### Audio playback
1. User selects stream (web UI / MPD / rotary)
2. `Player::startStream(url, name)` validates URL (must be `http://` or `https://`)
3. `audio->connecttohost(url)` — ESP32-audioI2S opens TCP, issues HTTP GET
4. Audio task (core 0) calls `audio->loop()` every 1 ms: buffer → decode → I2S
5. Library fires callbacks in main context:
   - `audio_showstreamtitle()` → `playerState.streamTitle`
   - `audio_showstation()` → `playerState.streamName`
   - `audio_bitrate()` → `playerState.bitrate` (bps → kbps)
   - `audio_icyurl()` / `audio_info()` → ICY metadata, cover art URL

### Status updates
- Every 3 s: main loop broadcasts JSON status to all WebSocket clients
- On any state change: `sendStatusToClients()` is called immediately
- MPD idle mode: hash-based change detection (title hash, status hash) sends `changed: player/playlist/mixer`

---

## Key Data Structures

### `Config` (main.h)
Hardware pin mappings and display settings, loaded from `/config.json` at boot. Editable via web UI without recompile.

### `PlayerState` (player.h)
```
playing, volume (0–22), bass/mid/treble (-6..+6)
playlistIndex, playStartTime, totalPlayTime, dirty
```

### `StreamInfoData` (player.h)
```
url[256], name[128], title[128], icyUrl[256], iconUrl[256], bitrate
```

### `StreamInfo` / `Playlist` (playlist.h)
Array of up to 20 `{name[96], url[128]}` entries, serialized as `/playlist.json`.

---

## SPIFFS Storage Layout

| File | Content | Max size |
|------|---------|----------|
| `/config.json` | Hardware pin config | 1 KB |
| `/wifi.json` | WiFi networks (array, max 5) | 2 KB |
| `/playlist.json` | Radio stations (array, max 20) | 4 KB |
| `/player.json` | Playback state | 512 B |
| `/player.html` etc. | Web UI assets | varies |

Writes use a backup/rollback pattern: existing file copied to `.bak`, new file written, `.bak` removed on success. SPIFFS is unmounted during OTA updates.

---

## HTTP API (port 80)

| Method | Endpoint | Purpose |
|--------|----------|---------|
| GET/POST | `/api/player` | Status / play·stop control |
| GET/POST | `/api/mixer` | Volume (0–22), bass/mid/treble |
| GET/POST | `/api/streams` | Read / replace playlist |
| GET/POST | `/api/config` | Hardware config |
| GET | `/api/config/export` | Export all configs |
| POST | `/api/config/import` | Bulk config import |
| GET | `/api/wifi/scan` | Scan WiFi networks |
| POST | `/api/wifi/save` | Save credentials |
| GET | `/api/wifi/status` | Connection status |
| GET | `/api/proxy` | Proxy remote playlist URLs |
| GET/POST | `/w` | Simple fallback HTML interface |

Static assets served from SPIFFS via `server.serveStatic()`.

---

## MPD Protocol (port 6600, MPD 0.23.0)

Custom implementation in `mpd.cpp`. Non-blocking: reads are async, no `delay()`.

**Supported command categories:**

| Category | Commands |
|----------|----------|
| Playback | `play`, `playid`, `stop`, `pause`, `next`, `previous` |
| Volume | `setvol` (0–100), `getvol`, `volume` |
| Status | `status`, `currentsong`, `stats` |
| Playlist | `playlistinfo`, `playlistid`, `lsinfo`, `listallinfo` |
| Search | `search`, `find` |
| System | `ping`, `commands`, `tagtypes`, `outputs` |
| Special | `idle`/`noidle`, `command_list_begin`/`end` |

Volume scale conversion: MPD 0–100 ↔ ESP32-audioI2S 0–22.

Command lists buffer up to 20 commands; safety cap at 50 to prevent memory exhaustion.

---

## Concurrency Model

| Task | Core | Priority | Period |
|------|------|----------|--------|
| Main loop | 1 | default | 150 ms |
| Audio task | 0 | 5 | 1 ms (`vTaskDelay`) |

Shared `PlayerState` protected by `portMUX_TYPE spinlock`. Audio callbacks run in the main-loop context. `yield()` calls prevent watchdog timeouts in long operations.

---

## Library Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| `esphome/ESP32-audioI2S` | ^2.3.0 | Audio decode + I2S streaming |
| `bblanchon/ArduinoJson` | ^7.4.2 | JSON config/API |
| `adafruit/Adafruit SSD1306` | ^2.5.15 | OLED display |
| `links2004/WebSockets` | ^2.3.6 | WebSocket server |
| `ESPmDNS` | built-in | mDNS (`CubeRadio.local`) |
| `WebServer` | built-in | HTTP server |
| `ArduinoOTA` | built-in | OTA firmware updates |
| `SPIFFS` | built-in | Flash filesystem |

---

## Default Pin Assignments (WROOM)

| Function | GPIO |
|----------|------|
| I2S DOUT | 25 |
| I2S BCLK | 27 |
| I2S LRC | 26 |
| OLED SDA | 21 |
| OLED SCL | 22 |
| Rotary CLK | 18 |
| Rotary DT | 19 |
| Rotary SW | 23 |
| Touch Play | 12 |
| Touch Next | 13 |
| Touch Prev | 14 |
| Board BTN | 0 |
| LED | 2 |

All pins are overridable through the web config UI and persisted to `/config.json`.

---

## Error Recovery

- **WiFi lost**: reconnect attempt every 60 s; soft-AP always available as fallback
- **Stream stops unexpectedly**: main loop detects `isPlaying && !isRunning`, waits 1 s, calls `startStream()` again
- **SPIFFS mount fails**: `SPIFFS.format()` then retry; falls back to compiled-in defaults
