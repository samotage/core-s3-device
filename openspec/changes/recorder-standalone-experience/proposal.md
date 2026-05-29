## Why

The CoreS3 meeting recorder works as a bench prototype — 16 kHz mono WAV capture to SD is reliable, with power-loss safety and WiFi serving of recordings — but the user experience is incomplete. There is no battery indicator, no on-device file management, no proper navigation between screens, no power management for the 200 mAh battery, and no way to turn the device off without unplugging it. This change builds the standalone meeting recorder experience: persistent status bar, brand-first recorder screen, purpose-built menu (Recorder, Files, Settings, Sleep, Power Off), file housekeeping, screen auto-off, deep sleep, power off, critical-battery auto-save, and storage-full handling — so the device can go from pocket to meeting table to pocket without a computer.

Cloud upload to Headspace is explicitly out of scope (separate PRD).

## What Changes

### Status Bar (FR1–FR5, NFR2)
- Add a persistent status bar at the top of every screen showing battery percentage (numeric, derived from AXP2101 voltage via LiPo discharge curve), charging icon (AXP2101 charge status), enlarged glanceable WiFi indicator, and recording indicator (red dot when recording on a non-recorder screen).
- Status bar updates at least every 5 seconds without disrupting mic DMA buffer drain timing (33ms cadence).

### Recorder Screen Redesign (FR6–FR9)
- **BREAKING:** Replace existing recorder layout. otageLabs logo becomes the dominant visual element; record button becomes a compact pill-shaped control in brand copper accent positioned below the logo.
- Idle state shows only logo + record button + status bar. Recording state adds MM:SS timer, VU level bar, and the button changes to a red Stop control.
- Remove SD card free-space readout from the recorder screen (lives on File Management screen now).

### Main Menu (FR10–FR12)
- **BREAKING:** Replace the existing 9-tile factory demo menu (Camera, IMU, I2C, Touch, SD, RTC, Power, WiFi, …) with a purpose-built 5-entry menu: Recorder, Files, Settings, Sleep, Power Off.
- Factory demo page code remains in firmware but is not wired into navigation.
- Every sub-screen (Recorder, Files, Settings) has a Back/Menu button that returns to the main menu.

### File Management Screen (FR13–FR17, NFR3)
- New `AppFiles` page showing total recording file count, storage used / total, and estimated recording time remaining (computed from free space and the ~115 MB/hour rate at 16 kHz mono 16-bit).
- "Delete oldest 10" action that batch-deletes the 10 oldest recordings by filename sort order, behind a confirmation dialog.
- Long SD operations must show a progress indicator or at minimum not freeze the UI.

### Settings Screen (FR18)
- New `AppSettings` page exposing a brightness control wired to `M5.Display.setBrightness()`.

### Screen Auto-Off (FR19–FR21)
- After 1 minute of no touch input, the display backlight turns off (`M5.Display.setBrightness(0)`).
- Any touch input wakes the screen and resets the idle timer.
- Recording continues uninterrupted while the screen is off — mic capture, SD write, and WAV header flush are unaffected.

### Deep Sleep (FR22–FR25)
- "Sleep" menu entry puts the ESP32-S3 into deep sleep via `esp_deep_sleep_start()`.
- Wake sources: touch interrupt (FT6336U, subject to hardware verification) and AXP2101 power-key press.
- On wake the device reboots to the recorder screen within 4 seconds.
- Sleep is disabled or hidden when recording is active.

### Power Off (FR26–FR29)
- "Power Off" menu entry triggers an AXP2101 hardware shutdown behind a confirmation dialog.
- Device powers on via the physical power button (AXP2101 hardware-handled).
- If recording is active, auto-save (finalise WAV header, flush, close file) before shutdown.

### Critical Battery Protection (FR30–FR32, NFR1)
- Monitor battery voltage during recording via AXP2101.
- At ~8–10% (≈3.3 V LiPo, exact threshold tuned on hardware), auto-save the recording then perform AXP2101 graceful shutdown to prevent FAT corruption.

### Storage Full Handling (FR33–FR34)
- Below a minimum free-space threshold (e.g. 5 MB), block recording: disable the record button and show a "storage full" message that offers navigation to the File Management screen.

### Recording Architecture Lift (FR35)
- **BREAKING:** Promote `AppRecorderModel` (mic capture, SD write state, recording flag, WAV state) from page-owned to app-level scope. Pages observe / control the model; they do not own its lifecycle. `MicEnd()` must no longer fire on page unload while recording is active.

### Non-Functional (NFR1–NFR4)
- All power transitions must leave the SD filesystem consistent (no partial writes, no corrupted FAT entries).
- Status bar refresh must not impact mic DMA timing.
- Long SD operations must keep the UI responsive.
- Brand styling stays on the existing palette (`COL_BG`, `COL_COPPER`, `COL_RED`, `COL_OFFWHITE`, `COL_GREY`, `COL_SURFACE`).

## Impact

### Affected specs

- `recorder-standalone-experience` (new capability) — covers status bar, recorder UI redesign, main menu replacement, file management, settings, screen auto-off, deep sleep, power off, critical battery protection, storage full handling, and the app-level recording model.

### Affected code

- `src/App.cpp`, `src/App.h` — host the promoted app-level recording model and the idle/status timers; own brightness state.
- `src/main.cpp` — boot path wires the new menu, status bar root, and brightness defaults; replaces hardcoded `setBrightness(60)` with the persisted Settings-controlled value.
- `src/pages/AppFactory.cpp`, `src/pages/AppFactory.h` — register new pages (`AppFiles`, `AppSettings`); remove factory demo pages from the menu mapping (keep classes registered for now).
- `src/pages/HomeMenu/*` — **BREAKING** rewrite of the menu model and view to the 5-entry purpose-built layout.
- `src/pages/AppRecorder/AppRecorder*.{cpp,h}` — recorder screen redesign (logo hero, pill button, timer, VU bar); read/write the app-level recording model instead of owning it.
- `src/pages/AppRecorder/AppRecorderModel.{cpp,h}` — lifecycle change: constructed at app init, not on page load; `MicEnd()` no longer called by page unload while recording.
- `src/pages/AppPower/AppPowerModel.{cpp,h}` — extend with battery-percentage-from-voltage (LiPo curve), charging-status read helper, AXP2101 power-off helper, and deep-sleep entry helper. Existing `AxpAdcSampling()` and `AxpBatIsCharging()` are reused.
- `src/pages/AppFiles/*` — **NEW** page: file count, storage stats, est. recording time, delete-oldest-10 with confirmation.
- `src/pages/AppSettings/*` — **NEW** page: brightness slider, back button.
- Shared status bar widget — **NEW** (likely `src/pages/_widgets/StatusBar.{cpp,h}` or `src/App` member) — rendered as a fixed top overlay across pages.
- `src/res/img/` — otageLabs logo asset for the recorder hero (verify presence; add if missing); recording-dot, charging, and enlarged WiFi icon assets.
- `include/config.h` — new firmware constants: `SCREEN_IDLE_TIMEOUT_MS` (60000), `STORAGE_FULL_THRESHOLD_BYTES` (e.g. 5_000_000), critical-battery threshold (percent and/or voltage), status-bar refresh interval.
- `src/net/RecorderServer.*` — unchanged (WiFi recording server still serves files; out-of-scope cloud upload not touched).
- `platformio.ini` — no changes expected (no new libraries anticipated; ESP-IDF deep-sleep APIs are already available).

### Behavioural / boundary notes

- Existing single-screen recorder behaviour is replaced; users coming from the prototype will see a different layout — this is intentional brand redesign.
- Recording-state persistence across navigation is a behavioural contract change: page transitions previously stopped recording; they no longer do.
- Out of scope (do not implement here, do not extend the menu for these): cloud upload to Headspace, NTP / timestamped filenames, on-device playback, file list / per-file detail, transcription, BLE, LED recording indicator, configurable auto-sleep timeout in UI, mic gain in UI, factory demo apps in navigation.
