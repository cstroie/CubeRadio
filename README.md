# CubeRadio

An ESP32-based internet radio player with web interface control

![CubeRadio](https://img.shields.io/badge/status-active-brightgreen)
![PlatformIO](https://img.shields.io/badge/platformio-latest-blue)
![License](https://img.shields.io/badge/license-GPL--3.0-blue)

## Overview

CubeRadio is an open-source internet radio player built on the ESP32 platform. It streams MP3/AAC/FLAC internet radio over HTTP or HTTPS and is controlled from a web interface, any MPD client or a physical rotary encoder. The project features an OLED display for local status feedback and supports playlist management through a web API.

## 🌟 Key Features

- **Internet Radio Streaming**: Play MP3, AAC and FLAC streams from HTTP/HTTPS URLs
- **Web Interface**: Control playback through a responsive web UI
- **Physical Controls**: Rotary encoder for volume control and navigation
- **OLED Display**: Real-time status information with scrolling text
- **Playlist Management**: Up to 100 radio stations, stored in flash (not RAM), with JSON/M3U/PLS import and export
- **Volume Control**: Adjustable volume through web interface or rotary encoder
- **WiFi Configuration**: Web-based WiFi setup with network scanning and multiple network support
- **File Management**: Upload/download playlists in JSON, JSON Lines, M3U, or PLS formats
- **WebSocket Communication**: Real-time status updates between device and web interface
- **MPD Protocol Support**: Control via MPD clients (port 6600) with full command list support
- **Cover Art**: Station artwork from stream metadata and artist images, loaded by the browser
- **ICY Metadata**: Station name and stream title from ICY headers and metadata
- **Artist/Track Parsing**: Automatic parsing of artist and track information from stream titles
- **Enhanced Status Information**: Detailed playback information including bitrates and elapsed time

## 🛠 Hardware Requirements

- ESP32 development board
- I2S DAC (e.g., MAX98357A) or amplifier
- SSD1306 OLED display (128x64 or 128x32)
- Rotary encoder with push button
- Audio amplifier and speaker

### Pin Connections

| Component         | ESP32 Pin |
|-------------------|-----------|
| I2S BCLK          | GPIO 27   |
| I2S LRC           | GPIO 25   |
| I2S DOUT          | GPIO 26   |
| OLED SDA          | GPIO 21   |
| OLED SCL          | GPIO 22   |
| Rotary CLK        | GPIO 18   |
| Rotary DT         | GPIO 19   |
| Rotary SW         | GPIO 23   |
| Board button      | GPIO 0    |
| LED               | GPIO 2    |

> **Note**: These are the WROOM defaults. Default pins come from `src/pins_wroom.h`, `pins_wrover.h` or `pins_cam.h` (selected by the PlatformIO environment) and can be overridden through the web configuration page.

## 🚀 Getting Started

### Prerequisites

1. Install [PlatformIO](https://platformio.org/)
2. Clone this repository:
   ```bash
   git clone https://github.com/cstroie/CubeRadio.git
   cd CubeRadio
   ```

### Building and Uploading

1. Build and upload the firmware:
   ```bash
   pio run -t upload
   pio run -t uploadfs
   ```
   `uploadfs` stores the web assets gzipped (see `tools/gzip_data.py`); edit the uncompressed files in `data/`.

2. If no configured network is reachable, the device starts an open access point named `CubeRadio`; connect to it and open the IP shown on the display. Once on your network it is reachable at `http://cuberadio.local/`

3. Configure your WiFi networks through the web interface. To preconfigure, copy `data/wifi.json.example` to `data/wifi.json` (git-ignored) before `uploadfs`.

## 🌐 Web Interface

Once connected to WiFi, access the web interface by navigating to the ESP32's IP address in a web browser.

### Main Controls
- **Play/Pause**: Start or stop playback of the selected stream
- **Volume Control**: Adjust volume through slider or rotary encoder
- **Playlist Management**: Add, remove, and organize radio stations
- **WiFi Configuration**: Configure multiple WiFi networks with priority ordering

### Playlist Management
- Up to 100 stations, stored on the device as JSON Lines and read on demand
- Upload/download playlists in JSON, JSON Lines, M3U, or PLS formats (conversion happens in the browser)
- Import stations from a remote playlist URL
- Manage individual streams through the web interface
- Real-time validation of stream URLs and names

### WiFi Configuration
- Scan for available networks
- Configure multiple WiFi networks with priority
- Automatic fallback to next available network
- Secure password storage

### API Endpoints

| Endpoint                  | Method | Description                           |
|---------------------------|--------|---------------------------------------|
| `/`                       | GET    | Main control interface                |
| `/playlist`               | GET    | Playlist management                   |
| `/config`                 | GET    | Hardware configuration                |
| `/wifi`                   | GET    | WiFi configuration                    |
| `/about`                  | GET    | About page                            |
| `/w`                      | GET/POST | Simple web interface (no JavaScript) |
| `/api/streams`            | GET    | Get the playlist (JSON Lines)         |
| `/api/streams`            | POST   | Replace the playlist (JSON Lines body)|
| `/api/player`             | GET/POST | Playback control and player status  |
| `/api/mixer`              | GET/POST | Volume and bass/mid/treble          |
| `/api/proxy`              | GET    | Proxy plain HTTP requests (remote playlists, cover art) |
| `/api/config`             | GET    | Get current configuration             |
| `/api/config`             | POST   | Update configuration                  |
| `/api/config/import`      | POST   | Import config, WiFi and player settings |
| `/api/wifi/scan`          | GET    | Scan for WiFi networks                |
| `/api/wifi/save`          | POST   | Save WiFi configuration               |
| `/api/wifi/status`        | GET    | Get current WiFi status               |
| `/api/wifi/config`        | GET    | Get current WiFi configuration        |

> **Note**: The WebSocket server on port 81 pushes status updates; the MPD server listens on port 6600.

## 📁 Project Structure

```
├── data/                 # SPIFFS content (pio run -t uploadfs)
│   ├── player.html       # Main control interface
│   ├── playlist.html     # Playlist management
│   ├── wifi.html         # WiFi configuration
│   ├── config.html       # Hardware configuration
│   ├── about.html        # About page
│   ├── scripts.js        # Shared JavaScript
│   ├── styles.css        # Shared styles
│   ├── pico.min.css      # PicoCSS framework
│   ├── cd.svg            # Logo and favicon
│   ├── playlist.jsonl    # Default stations (JSON Lines)
│   └── wifi.json.example # WiFi credentials template
├── src/
│   ├── main.cpp/h        # Setup, main loop, HTTP/WebSocket handlers
│   ├── player.cpp/h      # Playback state and audio control
│   ├── playlist.cpp/h    # Playlist stored in SPIFFS
│   ├── mpd.cpp/h         # MPD protocol server
│   ├── display.cpp/h     # OLED display
│   ├── rotary.cpp/h      # Rotary encoder
│   ├── touch.cpp/h       # Touch buttons (compiled out by default)
│   ├── pins*.h           # Per-board default pins
│   └── Spleen*.h         # Bitmap fonts
├── tools/
│   └── gzip_data.py      # Gzips web assets for the SPIFFS image
├── platformio.ini        # PlatformIO configuration
├── ARCHITECTURE.md       # Design overview
└── CLAUDE.md             # Developer notes
```

## 📜 License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

## 📝 Changelog

### Unreleased
- Playlist stored in SPIFFS as JSON Lines (`/playlist.jsonl`) and read on demand; limit raised from 20 to 100 stations. An existing `playlist.json` is migrated at first boot
- `/api/streams` GET and POST use JSON Lines (`{"name":"…","url":"…"}` per line); an empty playlist is a single blank line
- `/w` page and playlist responses are sent in chunks; RAM use no longer grows with the playlist
- Configuration import no longer carries the playlist; the web UI uploads it separately
- Cover art is loaded directly by the browser, with the device proxy as fallback
- `streamIcyURL` removed from the status JSON
- `/api/proxy` accepts GET and plain `http://` URLs only
- Web assets are stored gzipped in SPIFFS and cached by the browser
- WiFi-save and config-import bodies are spooled to SPIFFS instead of RAM
- Removed ArduinoOTA support
- `data/wifi.json` is no longer tracked; use `data/wifi.json.example`

### v1.0.1
- Fixed MPD command list handling to properly execute buffered commands instead of always returning an error

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 🙏 Acknowledgments

- ESP32-audioI2S library by schreibfaul1 (esphome fork)
- ArduinoJson library by Benoit Blanchon
- SSD1306 library by Adafruit
- WebSocket library by Links2004
- PicoCSS for the beautiful UI framework
