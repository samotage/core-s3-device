# CoreS3 Project Handoff — 2026-05-26

## Context

Sam has an M5Stack CoreS3 Lite ($44.90 USD) and wants to build an internet-connected meeting recorder as the first project, with a broader platform for future experimentation. This document captures all research done so you can start building immediately.

## Device: M5Stack CoreS3 Lite

**Key specs:**
- SoC: ESP32-S3FN16R8, dual-core Xtensa LX7 @ 240 MHz
- RAM: 8 MB PSRAM + 512 KB on-chip SRAM
- Flash: 16 MB
- WiFi: 2.4 GHz 802.11 b/g/n
- Bluetooth: BLE 5.0
- Display: 2" IPS 320×240, capacitive touch (FT6336U)
- **Audio codec: ES7210** — dual-mic, I2S ADC, 8–96 kHz, 16/24/32-bit, gain 0–37.5 dB (this is the reason the meeting recorder is viable)
- Speaker amp: AW88298, 1W
- Camera: GC0308 VGA (640×480, 30fps)
- Storage: microSD slot
- IMU: BMI270 6-axis + BMM150 magnetometer
- Battery: 200 mAh LiPo (the main constraint — ~1–2 hrs recording)
- Expansion: single HY2.0-4P Grove port (I2C/GPIO) — the Lite trade-off vs. full CoreS3

**Lite vs. full CoreS3:** Same silicon, but Lite has 200 mAh (vs. 500 mAh), one Grove port (vs. 30-pin header + 2 Grove), and a magnetic backplate (vs. DIN Rail base). For the meeting recorder this is fine.

## Toolchain

**PlatformIO CLI installed globally** via pipx on Sam's machine:
```bash
pio --version  # 6.1.19
```

Sam works from Claude Code (terminal), so all build/flash/monitor commands run via Bash. `pio device monitor` is interactive so use with a timeout for serial debugging; everything else (build, upload, lib management) works cleanly.

**No USB driver needed** on macOS 12+ — ESP32-S3 native USB CDC, presents as `/dev/cu.usbmodem*`. To force download mode: hold reset button ~2 seconds until internal LED lights.

## Project Setup

The project lives at `/Users/samotage/dev/otagelabs/core-s3` and has been registered in Headspace.

**Recommended `platformio.ini`:**
```ini
[env:m5stack-cores3]
platform = espressif32
board = m5stack-cores3
framework = arduino
build_flags =
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DARDUINO_USB_MODE=1
lib_deps =
  m5stack/M5Unified
  m5stack/M5GFX
  pschatzmann/arduino-audio-tools
monitor_speed = 115200
```

**Key libraries:**
- `m5stack/M5Unified` — the current unified M5Stack library (replaces old device-specific libs); handles display, IMU, power management, audio abstraction
- `m5stack/M5GFX` — graphics layer
- `pschatzmann/arduino-audio-tools` — the gold-standard ESP32 audio pipeline library; has explicit ES7210 support (added 2025); handles I2S capture, WAV encoding, SD streaming, WebSocket sink

## Meeting Recorder Architecture

**Recommended approach: Hybrid (local SD buffer + WiFi upload)**

1. Capture I2S audio from ES7210 at 16 kHz / 16-bit mono (Whisper API format)
2. Buffer in PSRAM, write WAV chunks to microSD
3. POST chunks to transcription API over WiFi when available

**Transcription options:**
- OpenAI Whisper API — $0.006/min, simple multipart POST, no infrastructure
- Self-hosted `faster-whisper` — sub-second latency on modern CPU, zero cost
- Azure Cognitive Services — streaming real-time option if needed

**Audio pipeline reference:**
- ESP-IDF official example: `i2s_es7210_tdm` — 4-channel WAV-to-SD
- arduino-audio-tools: `I2SStream → WAVEncoder → SDStream` pattern

**Battery note:** 200 mAh = ~1–2 hrs recording with screen off. USB power bank is practical for extended sessions.

## Other Use Cases (for future work)

- **Wake-word + cloud LLM voice assistant** — TFLite keyword detection on-device, LLM over WiFi
- **Computer vision terminal** — GC0308 + ESP-WHO → face detection, QR scanning, attendance
- **9-axis motion logger** — BMI270 + BMM150 + RTC → timestamped CSV to SD
- **BLE peripheral** — BLE 5.0 + touch display → configurator or HID remote
- **I2C sensor hub** — Grove port chains CO2/humidity/air quality sensors, WiFi upload

## First Steps

1. Write `platformio.ini` with the config above
2. Write a minimal `src/main.cpp` — `M5.begin()`, display "hello" — and do a test build/flash to confirm toolchain + device comms
3. Wire up ES7210 audio capture and record a WAV to SD (use arduino-audio-tools I2S example)
4. Add WiFi + HTTP POST to Whisper API
5. Add touch UI for record/stop/status

## Ecosystem Notes

- M5Unified is the one library to rule them all — don't use the older `M5CoreS3` device library (deprecated)
- ESP-IDF path (C/CMake) is available if more capability is needed (ESP-ADF for AEC, VAD, noise suppression) but the Arduino + arduino-audio-tools path covers the meeting recorder well
- Internal I2C bus (G12 SDA, G11 SCL) — do not conflict with: BMI270 (0x69), AXP2101 PMU, BM8563 RTC, ES7210 (0x40), AW88298 amp, AW9523B IO expander, FT6336U touch (0x38), LTR-553 light sensor (0x23)
- Grove expansion port: GND, 5V, G2, G1 (I2C or GPIO)
