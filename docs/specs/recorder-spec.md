# Meeting Recorder — Requirements & Build Spec

**Device:** M5Stack CoreS3 (Lite) — ESP32-S3, ES7210 mic, microSD, 320×240 touch, BM8563 RTC
**Status:** Workshopped 2026-05-28. Awaiting brand assets to begin build.
**Owner:** Chip (firmware)

## 1. Product Intent

A clean, single-purpose meeting recorder. Press a big button, it records the room to
the microSD card as a transcription-ready WAV. Future: recordings reach Sam's computer
over WiFi for transcription. otageLabs-branded UI.

## 2. Locked Decisions

| Area | Decision |
|---|---|
| **Boot behaviour** | Device boots **straight into the recorder**. The factory demo (camera/IMU/I2C/etc.) stays in the firmware but is reachable only via a hidden gesture — kept for hardware testing, invisible in normal use. |
| **Audio format** | **16 kHz, mono, 16-bit PCM WAV.** ~115 MB/hour. Zero-conversion input for Whisper. |
| **Capture path** | M5Unified `M5.Mic` (drives the ES7210 over I2S) — already a project dependency, no new audio library. |
| **Storage** | WAV files on microSD (FAT32). PSRAM ring buffer decouples I2S capture from SD write latency. |
| **Power-loss safety** | WAV header re-flushed periodically during recording + boot-time recovery, so a yanked cable mid-meeting still leaves a playable file. |
| **Filenames** | Timestamped from the RTC when set (`REC_20260528_143000.wav`); sequential fallback (`REC_0001.wav`) if RTC unset. Unique + sortable — ready for WiFi upload later. |
| **Future transfer** | **WiFi upload** to Sam's machine/an endpoint. Out of scope for v1, but filenames/format are designed for it now. Likely brings NTP time-sync (accurate timestamps) when added. |

## 3. UI / UX

Three states, big-button-first, designed for glanceability across a meeting table.

- **Idle:** otageLabs head logo + wordmark, large circular **RECORD** button (brand
  accent), free SD space shown. "Tap to record."
- **Recording:** pulsing red indicator, large elapsed timer (`MM:SS`), live input-level
  VU bar (so you can see it's hearing the room), **STOP** button. Screen auto-dims after
  ~30 s to save battery/avoid burn-in — recording continues; tap to wake.
- **Saved:** confirmation — `Saved REC_xxxx.wav (mm:ss)` — then back to Idle.

## 4. Things We Need In Place

| # | Item | Owner | Status |
|---|---|---|---|
| 1 | **otageLabs head logo** (PNG/SVG source) + brand colours (hex) + font, if any | Sam → Chip | **Blocking — awaiting path** |
| 2 | microSD card inserted, FAT32-formatted | Sam (physical) | To confirm |
| 3 | RTC set to correct local time (for timestamped names) | Chip can set on build; or accept sequential names for v1 | Decide at build |
| 4 | Device at bench, plugged in (`/dev/cu.usbmodem114301` per platformio.ini) | Sam | Plugged in ✓ — confirm port |
| 5 | *(Future, WiFi upload)* WiFi credentials + a receiving endpoint on Sam's machine | Sam | Deferred — not v1 |

## 5. Build Phases

1. **Spike:** `M5.Mic` capture → write a fixed-length WAV to SD → confirm it plays on a
   computer. Proves the whole hardware chain end to end. *(bench-verify)*
2. **Recorder core:** start/stop, PSRAM buffering, header flush + recovery, filenames.
3. **UI:** the three branded screens above, big touch button, VU meter, timer.
4. **Boot wiring:** boot-to-recorder + hidden gesture to the factory demo.
5. **Polish:** auto-dim, SD-full / no-card handling, power-cycle test.

## 6. Open / Deferred

- WiFi upload mechanism + endpoint protocol (v2).
- NTP time-sync over WiFi (v2, pairs with upload).
- On-device transcription is **not** planned — transcription happens on Sam's computer.
