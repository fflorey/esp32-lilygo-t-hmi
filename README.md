# LILYGO T-HMI (ESP32-S3) Projects & Demos

This repository contains firmware projects and demos developed for the **LILYGO T-HMI** development board, powered by an **ESP32-S3** microcontroller with a 2.8" ST7789 TFT display (8-bit parallel) and an XPT2046 resistive touch controller.

---

## 📁 Project Structure

```
.
├── include/
│   ├── pins.h                 # Hardware pin definitions for LILYGO T-HMI
│   ├── secrets.h.example      # Template for Wi-Fi and API credentials
│   └── secrets.h              # (Local only, git-ignored) Your private credentials
├── lib/                       # Vendored device libraries & display drivers
│   ├── ArduinoJson/           # Fast JSON serialization/deserialization
│   ├── OneButton/             # Multi-click button state machine
│   ├── TFT_eSPI/              # High-speed TFT driver configured for T-HMI
│   ├── TJpg_Decoder/          # Fast JPEG image decoding for TFT displays
│   ├── arduino_xpt2046_library/ # SPI resistive touch controller driver
│   └── lvgl/                  # Light and Versatile Embedded Graphics Library
├── src/
│   └── main.cpp               # Active build target (default: Komoot Tour Viewer)
├── demos/
│   ├── 01_touch_draw/         # Demo 1: Interactive drawing canvas & touch calibration
│   └── 02_komoot_tours/        # Demo 2: Komoot GPS tour & workout viewer via REST API
├── platformio.ini             # PlatformIO build configuration
└── .gitignore                 # Pre-configured ignore rules for PlatformIO & secrets
```

---

## 🚀 Demos

### 1. [Demo 1: Interactive Touch & Draw](demos/01_touch_draw/README.md)
* **Path:** `demos/01_touch_draw/`
* **Features:**
  - Interactive on-screen drawing canvas with color palette (Red, Green, Blue, Yellow, Clear).
  - Nintendo DS-style 2-point crosshairs calibration wizard with persistent calibration stored in ESP32 Flash NVS (`Preferences`).
  - Press the physical **BOOT button** (GPIO 0) at any time to re-calibrate touch.

### 2. [Demo 2: Komoot Tour & Training Viewer](demos/02_komoot_tours/README.md)
* **Path:** `demos/02_komoot_tours/` (also currently mirrored in `src/main.cpp`)
* **Features:**
  - Connects to Wi-Fi and fetches your latest completed tours (cycling, hiking, running) via the Komoot REST API.
  - Downloads and renders tour map thumbnail previews in JPEG format.
  - Interactive touch UI to page through tours (`PREV`, `NEXT`) and trigger a manual sync (`SYNC`).
  - Displays workout statistics: distance, moving time, elevation gain, avg speed, and calories burned.
  - Falls back to bundled offline sample data if Wi-Fi or credentials are unavailable.

---

## ⚙️ Configuration & Setup

### 1. Credentials (Wi-Fi & API)
For demos that require network access:
1. Copy the example credentials file:
   ```bash
   cp include/secrets.h.example include/secrets.h
   ```
2. Open `include/secrets.h` and fill in your Wi-Fi and Komoot credentials:
   ```cpp
   #define WIFI_SSID       "Your_WiFi_SSID"
   #define WIFI_PASSWORD   "Your_WiFi_Password"

   #define KOMOOT_EMAIL    "your_email@example.com"
   #define KOMOOT_PASSWORD "your_komoot_password"
   ```
   > [!NOTE]
   > `include/secrets.h` is excluded by `.gitignore` to prevent leaking private credentials.

### 2. Switching Between Demos
In [platformio.ini](platformio.ini), adjust the `src_dir` setting under `[platformio]`:
```ini
[platformio]
default_envs = t-hmi
; Select the active demo:
; src_dir = demos/01_touch_draw
src_dir = demos/02_komoot_tours
```

---

## 🛠️ Building and Flashing

Using the [PlatformIO](https://platformio.org/) CLI or VS Code extension:

```bash
# Build firmware
pio run -e t-hmi

# Upload to board
pio run -e t-hmi -t upload

# Open serial monitor
pio device monitor -b 115200
```
