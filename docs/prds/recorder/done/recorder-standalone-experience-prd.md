---
validation:
  status: valid
  validated_at: '2026-05-29T14:01:09+10:00'
---

## Product Requirements Document (PRD) — Recorder Standalone Experience

**Project:** CoreS3 Meeting Recorder
**Scope:** Complete standalone SD card recording experience with power management, navigation, file housekeeping, and status visibility
**Author:** Robbo (workshopped with Sam)
**Status:** Draft

---

## Executive Summary

The CoreS3 meeting recorder currently works as a bench prototype — it records audio to SD card with a single-screen UI. This PRD specifies the features needed to make it a polished, self-contained meeting recorder that can go from pocket to meeting table to pocket without needing a computer for file management or worrying about battery, storage, or power.

The device doubles as an otageLabs brand showcase. The recorder screen is designed brand-first: the otageLabs logo is the hero element, the record button is understated, and the UI stays clean. A persistent status bar provides battery, WiFi, and recording state across all screens. Purpose-built navigation replaces the factory demo menu with Recorder, Files, Settings, Sleep, and Power Off.

Cloud upload to Headspace is explicitly out of scope — that is a separate follow-on PRD.

---

## 1. Context & Purpose

### 1.1 Context

The recorder hardware works: 16 kHz mono WAV capture to SD card is reliable, with power-loss safety, auto-rollover on mic failure, and WiFi serving of recordings. But the user experience is incomplete. There is no battery indicator, no way to manage recordings on the device, no proper navigation between screens, no power management for extending battery life between meetings, and no way to turn the device off without unplugging it. The 200 mAh battery makes power management critical — every milliamp matters.

### 1.2 Target User

Sam (otageLabs founder) using the device in client meetings, workshops, and internal sessions. The device sits on a meeting table and must look professional, function reliably, and survive a full meeting on battery.

### 1.3 Success Moment

Sam pulls the CoreS3 from his pocket, taps the screen to wake it, glances at the battery percentage, hits Record, and puts it on the table. The screen goes dark after a minute. After the meeting, he taps to wake, stops recording, navigates to Files to check storage, then puts it to sleep. The otageLabs branding is visible every time someone glances at the device.

---

## 2. Scope

### 2.1 In Scope

- Persistent status bar across all screens (battery %, charging icon, WiFi, recording indicator)
- Recorder screen redesign (brand-first layout, pill-shaped record button, clean idle state)
- Purpose-built main menu (Recorder, Files, Settings, Sleep, Power Off)
- File management screen (file count, storage stats, estimated record time, batch delete)
- Settings screen (brightness control)
- Screen auto-off after 1 minute idle with touch-to-wake
- Deep sleep mode (manual, from menu) with touch/button wake
- Power off (manual, from menu) via AXP2101 shutdown
- Critical battery auto-save and graceful shutdown
- Storage full detection and prevention
- Confirmation dialogs for destructive actions
- Recording state persistence across page navigation
- Back button navigation on all sub-screens

### 2.2 Out of Scope

- Cloud/WiFi upload to Headspace (separate PRD)
- NTP time sync / timestamped filenames (pairs with cloud upload)
- Playback of recordings (parked for future PRD)
- File list / individual file detail view
- On-device transcription
- BLE connectivity
- LED recording indicator (no front-facing LED on CoreS3 Lite hardware)
- Configurable auto-sleep timeout in Settings UI (firmware constant for now; configurable later)
- Mic gain / sensitivity adjustment in Settings
- Factory demo apps in navigation (code remains in firmware, not wired to menu)

---

## 3. Success Criteria

### 3.1 Functional Success Criteria

- SC1: Battery percentage and charging state are visible on every screen
- SC2: WiFi status is visible on every screen at a glanceable size
- SC3: Screen turns off after 1 minute of no touch; touch wakes it; recording is uninterrupted
- SC4: Deep sleep from menu; device wakes on touch or button press and reaches recorder screen in under 4 seconds
- SC5: Power off from menu; device powers on via physical button press
- SC6: File management screen shows file count, storage used/available, and estimated recording time remaining
- SC7: "Delete oldest 10" removes the 10 oldest recordings after user confirms
- SC8: Device prevents recording when storage is full and tells the user why
- SC9: Recording auto-saves cleanly at critical battery before graceful shutdown
- SC10: User can navigate between Recorder, Files, and Settings via main menu with back buttons on every sub-screen
- SC11: Recording indicator (red dot) visible in status bar when recording and user navigates to other screens
- SC12: otageLabs logo is the dominant visual element on the recorder screen

---

## 4. Functional Requirements (FRs)

### Status Bar

- **FR1:** A persistent status bar is displayed at the top of every screen, showing: battery percentage, charging indicator, WiFi status, and recording indicator.
- **FR2:** Battery percentage is derived from AXP2101 voltage reading and displayed as a numeric percentage (e.g. "72%").
- **FR3:** A charging icon is displayed when USB power is connected (detected via AXP2101 charge status register).
- **FR4:** WiFi indicator is sized for glanceability across a meeting table — significantly larger than the current 14px icon.
- **FR5:** When recording is active and the user navigates away from the recorder screen, a red recording dot is displayed in the status bar.

### Recorder Screen

- **FR6:** The recorder screen displays the otageLabs logo as the primary visual element. The logo is the brand showcase — it dominates the screen in idle state.
- **FR7:** The record button is a pill-shaped (rounded rectangle) button in the brand accent colour, positioned below the logo. Compact and understated.
- **FR8:** When recording: the button changes to a stop control, a timer (MM:SS) is displayed, and a VU level bar shows input level. When idle: the screen is clean — logo and button only (plus status bar).
- **FR9:** SD card free space is not displayed on the recorder screen (this information lives on the file management screen).

### Main Menu

- **FR10:** A purpose-built main menu provides navigation to: Recorder, Files, Settings, Sleep, and Power Off.
- **FR11:** The factory demo apps (Camera, IMU, I2C, Touch, SD, RTC, Power, WiFi) are not present in the menu. Their code remains in firmware but is not wired to any navigation path.
- **FR12:** Every sub-screen (Recorder, Files, Settings) has a back button that returns to the main menu.

### File Management Screen

- **FR13:** The file management screen displays the total number of recording files on the SD card.
- **FR14:** The file management screen displays total storage available and used on the SD card.
- **FR15:** The file management screen displays estimated recording time remaining, calculated from free space at the current recording rate (~115 MB/hour at 16 kHz mono 16-bit).
- **FR16:** A "Delete oldest 10" button removes the 10 oldest recording files (by filename sort order) from the SD card.
- **FR17:** A confirmation dialog ("Delete 10 oldest recordings?") is displayed before executing the batch delete. The delete only proceeds on explicit confirmation.

### Settings Screen

- **FR18:** The settings screen provides a brightness control that adjusts the display backlight level via `M5.Display.setBrightness()`.

### Screen Auto-Off (Display Sleep)

- **FR19:** The screen backlight turns off after 1 minute of no touch input. This timeout is a firmware constant (not user-configurable in this PRD).
- **FR20:** Any touch input wakes the screen (turns backlight back on and resets the idle timer).
- **FR21:** Recording continues uninterrupted during screen-off. The mic capture, SD write, and WAV header flush continue regardless of display state.

### Deep Sleep

- **FR22:** The "Sleep" option on the main menu puts the ESP32-S3 into deep sleep mode.
- **FR23:** The device wakes from deep sleep on touch (FT6336U interrupt, if hardware supports it as a deep sleep wake source) or physical power button press (AXP2101 power key).
- **FR24:** On wake from deep sleep, the device reboots and reaches the recorder screen in under 4 seconds.
- **FR25:** Deep sleep is only available when not recording. If recording is active, the Sleep option is disabled or hidden.

### Power Off

- **FR26:** The "Power Off" option on the main menu triggers an AXP2101 hardware shutdown.
- **FR27:** A confirmation dialog ("Power off device?") is displayed before executing power off.
- **FR28:** The device powers on via the physical power button (AXP2101 handles this in hardware).
- **FR29:** If recording is active when power off is selected, the device first auto-saves the recording (finalise WAV header, flush, close file) before shutting down.

### Critical Battery Protection

- **FR30:** The device monitors battery voltage during recording via the AXP2101.
- **FR31:** When battery reaches a critical threshold (approximately 8-10%, corresponding to ~3.3V on the LiPo discharge curve — exact threshold to be tuned on hardware), the device auto-saves the current recording: finalises the WAV header, flushes buffered data, and closes the file.
- **FR32:** After auto-save, the device performs a graceful shutdown via AXP2101 to prevent data corruption from an uncontrolled power loss.

### Storage Full Handling

- **FR33:** When the SD card has insufficient free space to record (below a minimum threshold, e.g. 5 MB), the device prevents recording. The record button is disabled and a message explains that storage is full.
- **FR34:** The storage full message offers navigation to the file management screen where the user can perform a batch delete to free space.

### Recording Architecture

- **FR35:** The recording model (mic capture, SD write state, recording flag) operates at app-level scope rather than page-level scope. Recording state persists when the user navigates between the recorder screen, file management, settings, and main menu.

---

## 5. Non-Functional Requirements (NFRs)

- **NFR1:** All power management transitions (screen off, deep sleep, power off, critical battery shutdown) must leave the SD card filesystem in a consistent state. No partial writes or corrupted FAT entries.
- **NFR2:** The status bar must update battery percentage and WiFi state at a frequency that feels responsive (at least every 5 seconds) without impacting recording performance (mic DMA buffer drain timing is critical at 33ms cadence).
- **NFR3:** The UI must remain responsive during SD card operations (file counting, deletion). Long operations should show a progress indicator or at minimum not freeze the display.
- **NFR4:** Brand elements (otageLabs logo, accent colours, typography) must be consistent with the existing recorder screen design language (dark background, copper accent, warm off-white text).

---

## 6. UI Overview

### Screen Layout — All Screens

```
┌─────────────────────────────────┐
│ [BAT 72%] [⚡] [WiFi] [● REC]  │  ← Status bar (persistent)
├─────────────────────────────────┤
│                                 │
│         Screen content          │
│                                 │
│                                 │
│                                 │
│                                 │
│              [← Back]           │  ← Back button (sub-screens only)
└─────────────────────────────────┘
```

### Recorder Screen (Idle)

```
┌─────────────────────────────────┐
│ 72%  ⚡            ▂▄▆ WiFi    │
├─────────────────────────────────┤
│                                 │
│        ┌──────────────┐         │
│        │  otageLabs   │         │
│        │    (logo)    │         │
│        └──────────────┘         │
│                                 │
│       ┌──────────────────┐      │
│       │      Record      │      │  ← Pill-shaped button
│       └──────────────────┘      │
│                                 │
│              [← Menu]           │
└─────────────────────────────────┘
```

### Recorder Screen (Recording)

```
┌─────────────────────────────────┐
│ 72%  ⚡        ● REC  ▂▄▆ WiFi │
├─────────────────────────────────┤
│                                 │
│        ┌──────────────┐         │
│        │  otageLabs   │         │
│        │    (logo)    │         │
│        └──────────────┘         │
│           04:32                 │  ← Timer
│       ▓▓▓▓▓▓▓▓▓░░░░░░░░        │  ← VU bar
│       ┌──────────────────┐      │
│       │       Stop       │      │  ← Button changes to Stop (red)
│       └──────────────────┘      │
│                                 │
└─────────────────────────────────┘
```

### Main Menu

```
┌─────────────────────────────────┐
│ 72%  ⚡            ▂▄▆ WiFi    │
├─────────────────────────────────┤
│                                 │
│       ┌──────────────────┐      │
│       │    ● Recorder    │      │
│       └──────────────────┘      │
│       ┌──────────────────┐      │
│       │      Files       │      │
│       └──────────────────┘      │
│       ┌──────────────────┐      │
│       │    Settings      │      │
│       └──────────────────┘      │
│       ┌──────────────────┐      │
│       │     Sleep        │      │
│       └──────────────────┘      │
│       ┌──────────────────┐      │
│       │   Power Off      │      │
│       └──────────────────┘      │
│                                 │
└─────────────────────────────────┘
```

### File Management Screen

```
┌─────────────────────────────────┐
│ 72%  ⚡            ▂▄▆ WiFi    │
├─────────────────────────────────┤
│                                 │
│   Files:          23            │
│   Storage:   1.2 / 14.8 GB     │
│   Est. time:  ~118 hours        │
│                                 │
│       ┌──────────────────┐      │
│       │ Delete oldest 10 │      │
│       └──────────────────┘      │
│                                 │
│              [← Menu]           │
└─────────────────────────────────┘
```

### Confirmation Dialog (overlay)

```
┌─────────────────────────────────┐
│          ┌───────────────┐      │
│          │  Delete 10    │      │
│          │  oldest       │      │
│          │  recordings?  │      │
│          │               │      │
│          │  [Yes]  [No]  │      │
│          └───────────────┘      │
│                                 │
└─────────────────────────────────┘
```

---

## 7. Technical Context (for implementer)

This section provides implementation-relevant context. These are not requirements — they are guidance for the builder based on the existing codebase and hardware.

- **AXP2101 PMU** (`0x4A` on internal I2C): Battery voltage at registers `0x34-0x35`, charge status at `0x01` bit 5-6. Existing `AppPowerModel` has `AxpAdcSampling()` and `AxpBatIsCharging()` — these can be reused. Battery percentage must be derived from voltage via a LiPo discharge curve lookup (not a linear mapping — LiPo voltage is flat in the middle and drops sharply at the ends).
- **Display backlight**: `M5.Display.setBrightness(0)` for screen off, `M5.Display.setBrightness(N)` for on. Currently hardcoded to 60 in `main.cpp`.
- **Deep sleep**: `esp_deep_sleep_start()` with wake sources configured via `esp_sleep_enable_ext0_wakeup()` (for touch interrupt pin) or `esp_sleep_enable_ext1_wakeup()`. Touch controller (FT6336U) interrupt capability as a deep sleep wake source needs hardware verification.
- **AXP2101 power off**: Write to the power control register to trigger hardware shutdown. The AXP2101 power key handles wake in hardware — no software configuration needed.
- **Recording model promotion**: Currently `AppRecorderModel` is owned by the `AppRecorder` page and destroyed on page unload (`MicEnd()` called in `onViewUnload`). It needs to become a singleton or app-level object that pages reference but don't own.
- **Screen dimensions**: 320x240 pixels. Status bar should be compact (~20-24px high) to preserve content area.
- **Existing brand palette**: `COL_BG 0x1A1A1A`, `COL_COPPER 0xC78C5C`, `COL_RED 0xC75C5C`, `COL_OFFWHITE 0xEDE8E2`, `COL_GREY 0xA8A39D`, `COL_SURFACE 0x2A2A2A`.
