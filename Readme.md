# 🐦‍⬛ Crow Deterrent System

An ESP32-based automatic bird deterrent system that plays random sounds when motion is detected. 
Designed to scare away woodpeckers, crows, and other birds from wooden structures.

![Dashboard](dashboard.png)

## Features
- Motion detection via RCWL-0516 microwave radar sensor
- Random MP3 playback with history buffer (no immediate repeats)
- Configurable cooldown between triggers
- Web dashboard accessible via browser
- Adjustable volume and cooldown via sliders
- System on/off toggle
- Trigger statistics (today / total)
- NTP time sync

## Hardware

| Component | Description |
|---|---|
| ESP32 S2 Mini (Wemos) | Microcontroller |
| RCWL-0516 | Microwave radar motion sensor |
| DFPlayer Mini (MP3-TF-16P) | MP3 player module |
| Speaker 8Ω | Audio output |
| 1kΩ Resistor | Between ESP32 TX and DFPlayer RX |
| Solar Powerbank (5V USB) | Power supply |
| MicroSD Card | MP3 storage |

## Wiring

### RCWL-0516 → ESP32
| RCWL-0516 | ESP32 |
|---|---|
| VIN | VBUS (5V) |
| GND | GND |
| OUT | GPIO 7 |
| 3V3 | not connected |
| CDS | not connected |

### DFPlayer Mini → ESP32
| DFPlayer | ESP32 |
|---|---|
| VCC | VBUS (5V) |
| GND | GND |
| RX | GPIO 17 (via 1kΩ resistor) |
| TX | GPIO 18 |
| SPK_1 / SPK_2 | Speaker |

### Power
- Power the ESP32 via USB from a 5V solar powerbank
- VBUS and GND pins on ESP32 supply 5V to RCWL-0516 and DFPlayer

## SD Card Setup
Name your audio files as follows and place them in a folder named `mp3` on the SD card:
```
mp3/
├── 0001.mp3
├── 0002.mp3
├── 0003.mp3
└── ...
```

## Configuration
Edit these values at the top of `src/main.cpp`:

| Variable | Default | Description |
|---|---|---|
| `deviceName` | "Krähe 1" | Device name shown on dashboard |
| `SOUND_COUNT` | 7 | Number of MP3 files on SD card |
| `NO_REPEAT_LAST` | 5 | Minimum different sounds before repeat |
| `COOLDOWN_SEC` | 60 | Seconds between triggers (adjustable via dashboard) |
| `VOLUME` | 25 | Initial volume 0-30 (adjustable via dashboard) |
| `ssid` | "YOUR_WIFI" | WiFi network name |
| `password` | "YOUR_PASSWORD" | WiFi password |

## Installation

### Requirements
- VS Code with PlatformIO extension
- Git

### Steps
1. Clone the repository:
```bash
   git clone https://github.com/Psyobilin/crow-deterrent.git
```
2. Open in VS Code – PlatformIO detects the project automatically
3. Edit WiFi credentials and settings in `src/main.cpp`
4. Connect ESP32 S2 Mini via USB
5. Click Upload (→) in PlatformIO
6. Hold BOOT button when `Waiting for upload port...` appears
7. After flashing, check Serial Monitor (115200 baud) for the IP address
8. Open the IP address in your browser

## Dashboard
Access the web dashboard by entering the ESP32's IP address in your browser.

The page auto-refreshes every 10 seconds.

## Notes
- The RCWL-0516 sensor detects through plastic enclosures – ideal for weatherproof housings
- Wrap sensor partially with aluminium foil to reduce detection range/angle
- For multiple devices, change `deviceName` and `WiFi.setHostname()` for each unit
- NTP time sync requires WiFi connection