# 🎵 ESP32 Digital Harmonium Workstation

A full-stack embedded music workstation built around an **ESP32 microcontroller** and a custom binary sample format called **VHL (Virtual Harmonium Layout)**. Play a 61-key harmonium (or any sampled instrument) with real I2S audio output, design patches on a Rust/egui desktop app, and sync them to the hardware over Wi-Fi.

> **Author:** Gaurav Bhagwat  
> **Platform:** ESP32-WROOM-32 + PCM5102A DAC + 61-key matrix keyboard  
> **Status:** Active Development — V1.5.0

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Hardware Requirements & Wiring](#2-hardware-requirements--wiring)
3. [Repository Structure](#3-repository-structure)
4. [Prerequisites & Installation](#4-prerequisites--installation)
5. [Firmware (ESP32)](#5-firmware-esp32)
6. [HarmoniumWorkstation (Rust Desktop App)](#6-harmoniumworkstation-rust-desktop-app)
7. [Legacy VHL Builder (C++)](#7-legacy-vhl-builder-c)
8. [USB Audio Receiver (Python)](#8-usb-audio-receiver-python)
9. [Wi-Fi VHL Sync Workflow](#9-wi-fi-vhl-sync-workflow)
10. [Instruments & Sample Library](#10-instruments--sample-library)
11. [VHL v2 Format Overview](#11-vhl-v2-format-overview)
12. [Pushing to GitHub with PAT Token](#12-pushing-to-github-with-pat-token)
13. [Troubleshooting](#13-troubleshooting)
14. [License](#14-license)

---

## 1. Project Overview

```
Laptop (HarmoniumWorkstation app)
        │  Wi-Fi (192.168.4.1)   ← push VHL patch files
        │  USB Serial 921600      ← stream live audio back
        ▼
  ESP32-WROOM-32
        │  I2S (44.1 kHz, 16-bit stereo)
        ▼
   PCM5102A DAC  →  Speaker / Amplifier
        │
  61-key matrix keyboard (74HC165 shift register)
```

**What it does:**

- **ESP32 firmware** reads a `.vhl` binary patch from flash, scans a 61-key matrix keyboard at 1000 Hz, mixes up to 8 polyphonic voices, streams audio via I2S to a PCM5102A DAC, and serves a Wi-Fi HTTP API for over-the-air patch updates.
- **HarmoniumWorkstation** is a cross-platform Rust/egui desktop app for building and editing VHL patches — import WAV files, set loop points, tune regions, design SF2-style modulators, apply Freeverb reverb & chorus, then click **Sync** to push the patch to the ESP32 over Wi-Fi.
- **receiver.py** listens on USB serial and plays the ESP32 audio back through your laptop speakers in real time — useful for monitoring without a physical speaker connected.
- **Legacy VHL Builder** is an older C++ CLI tool that can also compile VHL files from `.sfz` + WAV sources.

---

## 2. Hardware Requirements & Wiring

### Components

| Component | Details |
|---|---|
| Microcontroller | ESP32-WROOM-32 / DevKit v1 |
| DAC | PCM5102A breakout module |
| Keyboard | 61-key reed organ keybed |
| Shift register | 74HC165 (parallel-in, serial-out) |
| USB | Silicon Labs CP210x (built into DevKit) |
| Power | 5 V / 1 A minimum (keyboard + DAC + ESP32) |

### Wiring Diagram

#### PCM5102A I2S DAC

| PCM5102A Pin | ESP32 GPIO | Notes |
|---|---|---|
| DIN | GPIO **25** | I2S Data Out |
| BCK | GPIO **26** | I2S Bit Clock |
| LRCK | GPIO **27** | I2S LR Clock |
| GND | GND | Common ground |
| VCC | 3.3 V | |
| FLT | GND | Filter normal mode |
| DEMP | GND | De-emphasis off |
| XSMT | 3.3 V | Unmute |
| FMT | GND | I2S format |

#### 61-Key Matrix Keyboard

| Signal | ESP32 GPIO | Notes |
|---|---|---|
| Row 0 | GPIO **23** | Drive row low to scan |
| Row 1 | GPIO **22** | |
| Row 2 | GPIO **21** | |
| Row 3 | GPIO **17** | |
| Row 4 | GPIO **16** | |
| Row 5 | GPIO **4** | |
| Row 6 | GPIO **13** | |
| Row 7 | GPIO **14** | |
| 74HC165 QH (serial out) | GPIO **34** | Reads column states |
| 74HC165 CLK | GPIO **18** | Shift clock |
| 74HC165 SHLD (latch) | GPIO **19** | Latch pulse |

> **Note:** Keyboard columns connect to the parallel inputs of one or more 74HC165 chips (daisy-chained for 8 columns). Pull-up resistors (10 kΩ) on each column line are recommended.

---

## 3. Repository Structure

```
Esp32 Digital Musical Workstation/
├── firmware/                        # ESP32 Arduino firmware
│   ├── firmware.ino                 # Main sketch — I2S, Wi-Fi AP, HTTP, keyboard, USB audio
│   ├── partitions.csv               # Custom partition table (larger app partition)
│   ├── src/                         # Arduino IDE source path
│   │   ├── vhl_core/include/        # vhl_structs.h, vhl_types.h, vhl_config.h, vhl_err.h
│   │   └── vhl_storage/             # vhl_loader.c/.h, vhl_harmonium_package.c
│   ├── components/                  # ESP-IDF component mirror (same files)
│   └── build/esp32.esp32.esp32/     # Pre-built binaries (.bin, .elf, .merged.bin)
│
├── HarmoniumWorkstation/            # Rust/egui desktop workstation app
│   ├── src/
│   │   ├── app.rs                   # App state, Wi-Fi sync (POST /api/upload_vhl)
│   │   ├── audio_engine.rs          # Soft synth, Freeverb reverb, chorus
│   │   ├── modulator_editor.rs      # SF2 modulator matrix editor
│   │   ├── landing.rs               # Landing/welcome page
│   │   ├── vhl/                     # compiler.rs, decompiler.rs, types.rs, wav_reader.rs
│   │   └── vhr/                     # VHR recorder / player
│   ├── Cargo.toml                   # eframe 0.29, egui 0.29, cpal 0.15, rfd 0.15
│   └── Cargo.lock                   # Locked dependency tree (committed for reproducibility)
│
├── Builder/                         # Legacy VHL Workbench (C++/CMake)
│   ├── main.cpp
│   ├── AssetProcessing/             # WavLoader, AssetProcessor, LookupTableBuilder
│   ├── Backend/                     # BinaryWriter, CRC32, Serializer, LayoutPlanner
│   ├── Frontend/ BuilderObjectModel/ Tools/
│   ├── build/bin/                   # vhl_builder.exe, vhl_reader.exe, vhl_workbench.exe
│   ├── vhr_decompiler.py            # Python VHR decompiler
│   └── vhr_receiver.py              # Python VHR receiver
│
├── Instruments/                     # Sample library
│   ├── Harmonium/                   # harmonium-c4.wav, harmomium_c4_reduced.wav
│   ├── harmonium new/               # Newer harmonium samples
│   ├── Natural Flute-1/             # Flute WAV samples + .sfz + .vhl
│   ├── Piano/                       # single-piano-note-c4.wav
│   ├── Tabla/                       # Tabla samples
│   └── samples/                     # Extra samples
│
├── Docs and context/                # Project documentation
│   ├── VHL_v2_SPECIFICATION_MASTER.md
│   ├── PROJECT_OVERVIEW_MASTER_ARCHITECTURE.md
│   └── MODULATOR_ARCHITECTURE_AND_IMPLEMENTATION_PLAN.md
│
├── phase1_vhl/                      # Phase 1 reference: architecture.md, vhl_structs.h
├── receiver.py                      # Python USB audio player (sounddevice + pyserial)
├── build_and_flash.ps1              # PowerShell: compile + flash firmware
├── run_workstation.ps1              # PowerShell: build + run HarmoniumWorkstation
└── .gitignore
```

> **Not in this repo:** `polyphone Source Code/` — kept locally only. Install Polyphone separately from [polyphone-soundfonts.com](https://www.polyphone-soundfonts.com).

---

## 4. Prerequisites & Installation

### 4.1 Arduino IDE (for Firmware)

1. Download **Arduino IDE 2.x** from [arduino.cc/en/software](https://www.arduino.cc/en/software)
2. Add the ESP32 board package:
   - Open **File → Preferences**
   - Add to "Additional boards manager URLs":
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Open **Tools → Board → Boards Manager**, search `esp32`, install **esp32 by Espressif Systems** (v2.x or v3.x)
3. Install required libraries via **Tools → Manage Libraries**:
   - `WebServer` (included with esp32 board package)
   - `WiFi` (included with esp32 board package)
   - `ArduinoJson` by Benoit Blanchon

### 4.2 Rust Toolchain (for HarmoniumWorkstation)

```powershell
# Install rustup (if not already installed)
winget install Rustlang.Rustup

# After installation, restart terminal, then:
rustup update stable
rustup target add x86_64-pc-windows-msvc
```

Minimum Rust version: **1.75+** (edition 2021)

### 4.3 Python 3 (for receiver.py)

```powershell
# Install Python 3.x from python.org or:
winget install Python.Python.3.12

# Install required packages
pip install sounddevice pyserial numpy
```

### 4.4 CMake + C++ Compiler (for Legacy Builder, optional)

```powershell
winget install Kitware.CMake
# Also install Visual Studio Build Tools or MinGW
```

### 4.5 esptool (for manual flashing)

```powershell
pip install esptool
```

---

## 5. Firmware (ESP32)

### 5.1 Open in Arduino IDE

1. Open `firmware/firmware.ino` in Arduino IDE 2.x
2. Go to **Tools → Board** → select **ESP32 Dev Module** (or your specific board)
3. Set **Tools → Port** to your COM port (e.g., `COM7` for CP210x)
4. Set **Tools → Upload Speed** → `921600`
5. Set **Tools → Partition Scheme** → `Custom` and point to `firmware/partitions.csv`

### 5.2 Important Firmware Macros

Open `firmware.ino` and adjust these defines near the top:

```c
#define WIFI_AP_SSID         "Harmonium-ESP32"   // Wi-Fi network name
#define WIFI_AP_PASS         "12345678"           // Wi-Fi password
#define ENABLE_AUTO_PLAY_SCALE  0                 // Set 1 to auto-play C4→C5 on boot
#define ENABLE_USB_AUDIO_STREAM 1                 // Set 1 to stream audio over USB
```

### 5.3 Flash via Arduino IDE

Click the **Upload** button (→ arrow) or press `Ctrl+U`.

### 5.4 Flash Pre-built Binary (no compilation needed)

If you want to flash without Arduino IDE, use esptool with the pre-built merged binary:

```powershell
# Replace COM7 with your actual port
esptool.py --chip esp32 --port COM7 --baud 921600 write_flash 0x0 "firmware\build\esp32.esp32.esp32\firmware.ino.merged.bin"
```

Or use the included PowerShell script:

```powershell
.\build_and_flash.ps1
```

### 5.5 What the Firmware Does

| Feature | Details |
|---|---|
| **Keyboard scan** | 61-key matrix, 8 rows × 8 cols via 74HC165, 1000 Hz scan rate |
| **Polyphony** | Up to 8 simultaneous voices with dynamic headroom scaling |
| **I2S audio** | 44100 Hz, 16-bit stereo via PCM5102A DAC |
| **Wi-Fi** | SoftAP — SSID `Harmonium-ESP32`, IP `192.168.4.1` |
| **HTTP API** | `GET /api/status`, `POST /api/upload_vhl` |
| **USB audio** | 921600 baud, downsampled to 22050 Hz, packetized `[0xA5][0x5A][...]` |
| **VHL loading** | Loads `.vhl` patch from SPIFFS/LittleFS on boot |

### 5.6 HTTP API Reference

| Endpoint | Method | Description |
|---|---|---|
| `/api/status` | GET | Returns JSON `{"status":"ok","vhl_loaded":true,"vhl_name":"..."}` |
| `/api/upload_vhl` | POST | Accepts raw VHL binary body, loads it into the synth engine |

---

## 6. HarmoniumWorkstation (Rust Desktop App)

### 6.1 Build and Run

```powershell
cd HarmoniumWorkstation
cargo build --release       # compile (first time takes ~2-5 min)
cargo run --release         # run the app
```

Or use the convenience script from the project root:

```powershell
.\run_workstation.ps1
```

### 6.2 Development / Debug Build

```powershell
cd HarmoniumWorkstation
cargo run                   # debug build (faster to compile, slower to run)
```

### 6.3 Run Tests

```powershell
cd HarmoniumWorkstation
cargo test                  # run all 20 unit tests
```

### 6.4 App Features

| Tab / Section | What it does |
|---|---|
| **Instrument** | Import WAV files, set root key, loop points, fine tune |
| **Regions** | Split the keyboard into up to 10 regions, each with its own sample |
| **Modulators** | SF2-style modulator matrix — map velocity / aftertouch → volume, pitch, cutoff, etc. |
| **Effects** | Freeverb reverb (room size, damping, width) + Chorus (rate, depth, mix) |
| **VHR** | Record and play back VHR (Virtual Harmonium Recording) sessions |
| **Sync** | Click the **status pill** (top-right) to push the current patch to the ESP32 over Wi-Fi |

### 6.5 Wi-Fi Sync (from App)

1. Connect your laptop to the `Harmonium-ESP32` Wi-Fi network (password: `12345678`)
2. In the app, click the coloured **status dot / pill** in the top-right corner
3. The app compiles the current patch to VHL binary and HTTP POSTs it to `http://192.168.4.1/api/upload_vhl`
4. On success the status pill turns green; on failure it turns red with an error message

### 6.6 Key Dependencies

```toml
eframe = "0.29"       # egui app framework
egui   = "0.29"       # immediate-mode UI
cpal   = "0.15"       # cross-platform audio
rfd    = "0.15"       # file dialogs
hound  = "3.5"        # WAV reading
```

---

## 7. Legacy VHL Builder (C++)

The `Builder/` directory contains an older command-line toolchain for compiling `.sfz` + WAV files into `.vhl` binaries. Pre-built executables are in `Builder/build/bin/`.

### 7.1 Use Pre-built Executables

```powershell
# Build a VHL from SFZ + WAVs
.\Builder\build\bin\vhl_builder.exe --input instruments\flute.sfz --output flute.vhl

# Inspect a VHL file
.\Builder\build\bin\vhl_reader.exe flute.vhl

# Launch the legacy workbench GUI
.\Builder\build\bin\vhl_workbench.exe
```

### 7.2 Build from Source (optional)

```powershell
cd Builder
cmake -B build -S .
cmake --build build --config Release
```

---

## 8. USB Audio Receiver (Python)

`receiver.py` listens on the USB serial port for audio packets from the ESP32 and plays them through your laptop speakers — so you can hear the instrument without a physical speaker connected to the DAC.

### 8.1 Run

```powershell
# Auto-detect ESP32 port and use default audio device
python receiver.py

# Specify COM port and output device index manually
python receiver.py --port COM7 --device 0

# List available audio output devices
python receiver.py --list-devices
```

### 8.2 Keyboard Controls (while running)

| Key | Action |
|---|---|
| `+` / `-` | Volume up / down |
| `q` | Quit |

### 8.3 Packet Format

The ESP32 sends audio in framed packets over serial at 921600 baud:

```
[0xA5] [0x5A] [sample_rate: uint16 LE] [num_samples: uint16 LE] [int16 PCM × num_samples]
```

Default: 22050 Hz, 32 samples per packet.

---

## 9. Wi-Fi VHL Sync Workflow

```
Step 1: Connect laptop to "Harmonium-ESP32" Wi-Fi (password: 12345678)
Step 2: Open HarmoniumWorkstation app (cargo run --release)
Step 3: Build or import your patch
Step 4: Click the status pill (top-right corner of the app)
Step 5: Wait ~1-2 seconds — green = success, red = error
Step 6: ESP32 auto-loads the new patch — play immediately!
```

### Troubleshooting Wi-Fi Sync

| Problem | Fix |
|---|---|
| Status goes offline after clicking sync | ESP32 resets Wi-Fi when loading a large VHL — wait 3 seconds and click sync again |
| `Response read error: forcibly closed` | Normal — ESP32 drops the TCP connection after processing. The upload succeeded if the status briefly showed green. |
| Wi-Fi drops after 2-3 minutes | Power saving was disabled in firmware (`WIFI_PS_NONE`). If still occurring, try a powered USB hub or dedicated 5 V supply |
| Cannot reach 192.168.4.1 | Ensure no other VPN is active on your laptop; check Windows Firewall |

---

## 10. Instruments & Sample Library

| Folder | Contents |
|---|---|
| `Instruments/Harmonium/` | `harmonium-c4.wav` (full quality), `harmomium_c4_reduced.wav` (smaller) |
| `Instruments/harmonium new/` | Newer harmonium recordings |
| `Instruments/Natural Flute-1/` | Multi-sample flute WAVs (`G3`, `C4`, `F4`, `A#4`, `D#5`, `G#5`) + `.sfz` + `.vhl` |
| `Instruments/Piano/` | `single-piano-note-c4.wav` |
| `Instruments/Tabla/` | Tabla drum samples |
| `Instruments/samples/` | Extra general samples |

To load an instrument into the app, use **File → Import VHL** (`.vhl`) or **File → Import SFZ** (`.sfz`) from the HarmoniumWorkstation menu.

---

## 11. VHL v2 Format Overview

**VHL (Virtual Harmonium Layout)** is a custom binary format designed for flash-friendly streaming on microcontrollers with limited RAM.

### Magic & Header

```
Bytes 0-3:   Magic = "VHL2"
Byte  4:     Major version = 2
Byte  5:     Minor version = 0
Bytes 6-127: Section offsets, sizes, flags, CRC32 (128-byte fixed header)
```

### Sections (in order in the file)

| Section | Description |
|---|---|
| `keybed_map` | 128-entry table mapping MIDI note → region index |
| `tuning_table` | Per-note pitch correction offsets |
| `instrument_table` | One `VHLInstrumentEntry` (48 bytes) per instrument |
| `region_table` | One `VHLRegion` (80 bytes) per region — root key, loop points, ADSR, etc. |
| `sample_directory` | One `VHLSampleEntry` (32 bytes) per sample — offset into PCM data |
| `lookup_table` | Pre-computed gain/pitch curves |
| `pcm_data` | Raw 16-bit signed PCM samples (mono or stereo) |
| `modulator_table` | One `VHLModulator` (12 bytes) per SF2-style modulator |

### Backward Compatibility

VHL v1 files (magic `VHL1`) are auto-detected and loaded with the legacy loader path. VHL v2 is the current standard.

See `Docs and context/VHL_v2_SPECIFICATION_MASTER.md` for the full byte-level specification.

---

## 12. Pushing to GitHub with PAT Token

If you get a `403: Write access not granted` or credential error, follow these exact steps.

### 12.1 Create a Personal Access Token (PAT)

1. Go to [github.com/settings/tokens](https://github.com/settings/tokens)
2. Make sure you are logged into the **Gaurav-Bhagwat-Prim** account
3. Click **Generate new token (classic)**
4. Name it (e.g., `esp32-workstation-push`)
5. Set expiration (90 days recommended)
6. Check the **`repo`** scope (full repository access)
7. Click **Generate token** — **copy it immediately**, you won't see it again!

### 12.2 Clear Cached Windows Credentials (prevents 403)

> [!IMPORTANT]
> If Windows Credential Manager cached credentials for a different GitHub account, every push will silently use the wrong account and fail with 403. Clear it first.

1. Open **Windows Credential Manager** (search in Start menu)
2. Click **Windows Credentials**
3. Find and **delete** any entries that start with `git:https://github.com`
4. Close Credential Manager

### 12.3 Push with Token Embedded in URL

```powershell
cd "d:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation"

# Step 1: Set the remote URL with your token embedded (replace YOUR_TOKEN)
git remote set-url origin https://YOUR_TOKEN@github.com/Gaurav-Bhagwat-Prim/Digital-Harmonium-Workstation.git

# Step 2: Push
git push -u origin main

# Step 3: After pushing succeeds, remove token from URL (security!)
git remote set-url origin https://github.com/Gaurav-Bhagwat-Prim/Digital-Harmonium-Workstation.git
```

> [!CAUTION]
> Never commit a remote URL with a token embedded in it. Always run Step 3 after a successful push to remove the token from the URL.

### 12.4 Daily Workflow (after first push)

```powershell
cd "d:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation"

git add -A
git commit -m "your message here"

# Temporarily embed token, push, then restore clean URL
git remote set-url origin https://YOUR_TOKEN@github.com/Gaurav-Bhagwat-Prim/Digital-Harmonium-Workstation.git
git push
git remote set-url origin https://github.com/Gaurav-Bhagwat-Prim/Digital-Harmonium-Workstation.git
```

### 12.5 What is NOT in the repo

| Path | Reason |
|---|---|
| `polyphone Source Code/` | Third-party code, not owned — install Polyphone separately |
| `HarmoniumWorkstation/target/` | Rust build artifacts — auto-generated, large |
| `__pycache__/` | Python bytecode — auto-generated |

---

## 13. Troubleshooting

### ESP32 won't show up on COM port
- Install the **CP210x USB to UART driver** from Silicon Labs: [silabs.com/developers/usb-to-uart-bridge-vcp-drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
- Try a different USB cable (some are charge-only)

### No sound from PCM5102A
- Check the `XSMT` pin is tied to 3.3 V (mute pin — must be HIGH for audio)
- Verify wiring against the table in Section 2
- In Arduino IDE Serial Monitor, look for `[I2S] DMA started` in the boot log

### `cargo build` fails on Windows
```powershell
# Install Visual Studio Build Tools (C++ workload)
winget install Microsoft.VisualStudio.2022.BuildTools
# Then in VS installer add: "Desktop development with C++"
```

### receiver.py — "No device found"
```powershell
python receiver.py --list-devices  # find your output device index
python receiver.py --port COM7 --device 1
```

### receiver.py — "serial.SerialException: could not open port"
- Make sure Arduino IDE Serial Monitor is closed (can't share port)
- Check Device Manager for the correct COM port number

---

## 14. License

This project is currently **unlicensed / all rights reserved**.

For academic or personal use inquiries, contact:  
📧 gaurav@bhagwat.com

---

*Built with ❤️ using ESP32, Rust, egui, and a lot of harmonium samples.*
