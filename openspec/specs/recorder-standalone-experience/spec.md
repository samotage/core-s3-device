# recorder-standalone-experience Specification

## Purpose
TBD - created by archiving change recorder-standalone-experience. Update Purpose after archive.
## Requirements
### Requirement: Persistent Status Bar
The device SHALL display a persistent status bar at the top of every screen showing battery percentage, charging indicator, WiFi status, and recording indicator. The status bar SHALL refresh at least every 5 seconds and MUST NOT disrupt microphone DMA buffer drain timing (33 ms cadence).

#### Scenario: Status bar visible on every screen
- **WHEN** the user is on the Recorder, Main Menu, Files, or Settings screen
- **THEN** a status bar at the top of the screen shows battery percentage, charging state, WiFi state, and (when applicable) recording state

#### Scenario: Battery percentage from voltage
- **WHEN** the AXP2101 voltage reading is sampled
- **THEN** the status bar displays a numeric battery percentage derived from a LiPo discharge-curve lookup (not a linear mapping)

#### Scenario: Charging icon when USB power present
- **WHEN** the AXP2101 charge status register reports charging
- **THEN** the status bar displays a charging icon

#### Scenario: WiFi indicator is glanceable
- **WHEN** the status bar is rendered
- **THEN** the WiFi indicator is significantly larger than the previous 14 px icon, sized for glanceability across a meeting table

#### Scenario: Recording indicator off the recorder screen
- **WHEN** a recording is active and the user is on a screen other than the Recorder
- **THEN** a red recording dot is displayed in the status bar

#### Scenario: Status bar refresh does not stall mic capture
- **WHEN** the status bar refreshes battery and WiFi state
- **THEN** the microphone DMA buffer drain at 33 ms cadence continues without overruns or dropouts

---

### Requirement: Recorder Screen Brand-First Layout
The recorder screen SHALL render the otageLabs logo as the dominant visual element with a pill-shaped record button in the brand copper accent positioned below the logo. In the idle state the screen SHALL show only the logo, the record button, and the status bar. SD card free-space data MUST NOT be displayed on the recorder screen.

#### Scenario: Idle state is brand-first
- **WHEN** the recorder screen is shown and no recording is active
- **THEN** only the otageLabs logo, the pill-shaped Record button, and the persistent status bar are visible

#### Scenario: Recording state shows timer, VU, and stop control
- **WHEN** a recording is active and the recorder screen is shown
- **THEN** the button changes to a red Stop control and an MM:SS timer plus a VU level bar are displayed

#### Scenario: SD free space not shown on recorder
- **WHEN** the recorder screen is shown in any state
- **THEN** SD card free-space information is not displayed on this screen

---

### Requirement: Purpose-Built Main Menu
The main menu SHALL provide exactly five navigation entries: Recorder, Files, Settings, Sleep, and Power Off. Factory demo apps (Camera, IMU, I2C, Touch, SD, RTC, Power, WiFi) MUST NOT be present in the menu; their firmware code remains compiled-in but unwired from navigation. Every sub-screen SHALL include a Back button that returns to the main menu.

#### Scenario: Five entries are present
- **WHEN** the user opens the main menu
- **THEN** the only entries shown are Recorder, Files, Settings, Sleep, and Power Off

#### Scenario: Factory demos are not reachable from navigation
- **WHEN** the user navigates the main menu
- **THEN** no Camera, IMU, I2C, Touch, SD, RTC, Power, or WiFi demo entry is reachable from the menu

#### Scenario: Back button on every sub-screen
- **WHEN** the user is on the Recorder, Files, or Settings sub-screen
- **THEN** a Back button is visible that returns the user to the main menu

---

### Requirement: File Management Screen
The Files screen SHALL display the total number of recording files on the SD card, the total and used storage, and the estimated recording time remaining computed from free space at the 16 kHz mono 16-bit rate (~115 MB/hour). It SHALL provide a "Delete oldest 10" action that requires explicit confirmation before deleting the 10 oldest recording files by filename sort order. The UI MUST remain responsive during file count and delete operations.

#### Scenario: File and storage stats are shown
- **WHEN** the Files screen is opened
- **THEN** it displays the total recording file count, the used/total storage, and the estimated recording time remaining

#### Scenario: Estimated time uses recording rate
- **WHEN** computing the estimated recording time remaining
- **THEN** the estimate is derived from free space divided by ~115 MB/hour (16 kHz mono 16-bit)

#### Scenario: Delete oldest 10 requires confirmation
- **WHEN** the user presses "Delete oldest 10"
- **THEN** a confirmation dialog ("Delete 10 oldest recordings?") is shown and the deletion only proceeds on explicit Yes

#### Scenario: Delete oldest 10 removes the right files
- **WHEN** the user confirms the deletion
- **THEN** exactly the 10 oldest recording files by filename sort order are removed from the SD card and the on-screen stats refresh

#### Scenario: UI remains responsive during SD work
- **WHEN** file counting or batch deletion runs
- **THEN** the UI shows a progress indicator or otherwise does not freeze the display

---

### Requirement: Settings Screen with Brightness Control
The Settings screen SHALL expose a brightness control that adjusts the display backlight via `M5.Display.setBrightness()`. The selected brightness SHALL be used as the active backlight value in place of the hardcoded boot default.

#### Scenario: Brightness control affects backlight
- **WHEN** the user changes the brightness control
- **THEN** the display backlight updates via `M5.Display.setBrightness()` to the chosen level

#### Scenario: Settings brightness replaces hardcoded default
- **WHEN** the device boots after a brightness selection has been made
- **THEN** the active backlight uses the Settings-controlled value rather than the prior hardcoded `60`

---

### Requirement: Screen Auto-Off with Touch-to-Wake
The display backlight SHALL turn off after 1 minute of no touch input. Any touch input SHALL wake the screen and reset the idle timer. Recording MUST continue uninterrupted while the screen is off.

#### Scenario: Backlight turns off after idle timeout
- **WHEN** no touch input has occurred for 60 seconds
- **THEN** the display backlight is turned off via `M5.Display.setBrightness(0)`

#### Scenario: Touch wakes screen and resets timer
- **WHEN** the user touches the screen while the backlight is off
- **THEN** the backlight returns to the Settings-selected brightness and the idle timer restarts

#### Scenario: Recording continues while screen is off
- **WHEN** the screen is off and a recording is active
- **THEN** microphone capture, SD writes, and WAV header flushing continue without interruption

---

### Requirement: Deep Sleep with Touch and Power-Key Wake
The Sleep main-menu entry SHALL put the ESP32-S3 into deep sleep via `esp_deep_sleep_start()`. The device SHALL wake from deep sleep on touch (FT6336U interrupt, subject to hardware verification) and on AXP2101 power-key press. On wake, the device SHALL reach the recorder screen within 4 seconds. The Sleep entry SHALL be disabled or hidden when a recording is active.

#### Scenario: Sleep entry triggers deep sleep
- **WHEN** the user selects "Sleep" from the main menu while not recording
- **THEN** the device calls `esp_deep_sleep_start()` and enters deep sleep

#### Scenario: Touch wakes from deep sleep
- **WHEN** the device is in deep sleep and the user touches the screen
- **THEN** the device wakes via the FT6336U interrupt wake source and boots to the recorder screen within 4 seconds

#### Scenario: Power key wakes from deep sleep
- **WHEN** the device is in deep sleep and the user presses the physical power button
- **THEN** the device wakes via the AXP2101 power-key wake source and boots to the recorder screen within 4 seconds

#### Scenario: Sleep disabled while recording
- **WHEN** a recording is active and the user opens the main menu
- **THEN** the Sleep entry is disabled or hidden

---

### Requirement: Power Off with Confirmation and Recording Auto-Save
The Power Off main-menu entry SHALL trigger an AXP2101 hardware shutdown behind a confirmation dialog ("Power off device?"). If a recording is active when Power Off is confirmed, the device SHALL first finalise the WAV header, flush buffered data, and close the file before shutting down.

#### Scenario: Power off requires confirmation
- **WHEN** the user selects "Power Off" from the main menu
- **THEN** a confirmation dialog ("Power off device?") is shown and shutdown only proceeds on explicit Yes

#### Scenario: Power off auto-saves an active recording
- **WHEN** a recording is active and Power Off is confirmed
- **THEN** the device finalises the WAV header, flushes buffered data, and closes the file before triggering AXP2101 shutdown

#### Scenario: Power on via physical button
- **WHEN** the device is off and the user presses the physical power button
- **THEN** the device powers on via the AXP2101 power key (see "Quick-Tap Power-On From Hardware-Off" for the press-duration requirement)

---

### Requirement: Quick-Tap Power-On From Hardware-Off
After a full AXP2101 hardware power-off (`PowerOff()` drops the system rails while the PMU stays alive on battery), the device SHALL power back on from a **short press** of the physical power key — a deliberate quick tap, not a multi-second hold. The firmware SHALL explicitly configure the AXP2101 power-key on-level (register 0x27, ONLEVEL field) to a short press duration during boot, rather than relying on the M5Unified default, so a library change cannot silently lengthen the required press. The configured register value SHALL be logged over serial at boot for verification.

Rationale: this is a portable field recorder. Requiring a multi-second hold to power on (the observed pre-fix behaviour) makes the device feel dead in the hand and is unacceptable for field use.

#### Scenario: Short press boots from hardware-off
- **WHEN** the device has been powered off via `PowerOff()` (battery present) and the user gives the power key a short tap
- **THEN** the device boots to the recorder screen — no multi-second hold is required

#### Scenario: Power-key configuration is explicit and verifiable
- **WHEN** the device boots
- **THEN** the firmware writes the AXP2101 power-key timing register (0x27) to a known ONLEVEL value and logs the before/after register value over serial

#### Scenario: USB-insertion power-on remains a recovery path
- **WHEN** the device is off and USB power is connected
- **THEN** the AXP2101 VBUS-insertion event powers the system on, providing a recovery path independent of the power-key press

---

### Requirement: Critical Battery Auto-Save and Graceful Shutdown
The device SHALL monitor battery voltage during recording via the AXP2101. When the battery reaches a critical threshold (approximately 8–10%, corresponding to ~3.3 V on the LiPo discharge curve, exact threshold tuned on hardware), the device SHALL auto-save the current recording — finalise the WAV header, flush buffered data, close the file — and then perform a graceful AXP2101 shutdown.

#### Scenario: Critical threshold triggers auto-save
- **WHEN** the AXP2101 reports a battery level at or below the configured critical threshold during recording
- **THEN** the device finalises the WAV header, flushes buffered data, and closes the recording file

#### Scenario: Graceful shutdown after auto-save
- **WHEN** the auto-save completes following a critical-battery event
- **THEN** the device performs a graceful AXP2101 shutdown to prevent data corruption from an uncontrolled power loss

---

### Requirement: Storage Full Prevention
The device SHALL prevent a recording from starting when SD free space is below a minimum threshold (e.g. 5 MB). The record button SHALL be disabled and the UI SHALL explain that storage is full and offer navigation to the File Management screen.

#### Scenario: Record blocked when storage is below threshold
- **WHEN** the SD card free space is below the configured threshold and the user is on the recorder screen
- **THEN** the record button is disabled and a "storage full" message is shown

#### Scenario: Storage-full message routes to File Management
- **WHEN** the storage-full message is shown
- **THEN** a control is provided that navigates the user to the File Management screen

---

### Requirement: App-Level Recording Model
The recording model (mic capture, SD write state, recording flag, WAV state) SHALL be owned at app scope rather than page scope. Recording state SHALL persist when the user navigates between the Recorder, Files, Settings, and Main Menu screens. Page unload MUST NOT terminate an active recording.

#### Scenario: Recording survives navigation away from recorder
- **WHEN** a recording is active and the user navigates from the Recorder screen to Files, Settings, or the Main Menu
- **THEN** the recording continues uninterrupted and the recording indicator appears in the status bar

#### Scenario: Recording survives navigation back to recorder
- **WHEN** the user returns to the Recorder screen after navigating away during an active recording
- **THEN** the recorder UI reflects the still-active recording state (timer continues, Stop control visible, VU bar live)

#### Scenario: Page unload does not call MicEnd while recording
- **WHEN** the Recorder page is unloaded while a recording is active
- **THEN** `MicEnd()` is not invoked and the mic, SD writer, and WAV state remain owned by the app-level model

---

### Requirement: Power-Transition Filesystem Consistency
All power management transitions — screen off, deep sleep entry, power off, critical-battery shutdown — SHALL leave the SD card filesystem in a consistent state with no partial writes and no corrupted FAT entries.

#### Scenario: SD filesystem consistent across screen off
- **WHEN** the screen turns off while a recording is active and the user later stops the recording
- **THEN** the resulting WAV file is well-formed and the FAT is uncorrupted

#### Scenario: SD filesystem consistent across deep sleep
- **WHEN** the device enters deep sleep and is later woken
- **THEN** the SD filesystem mounts cleanly and any prior recordings are readable

#### Scenario: SD filesystem consistent across power off
- **WHEN** the device performs a confirmed Power Off (with auto-save if recording)
- **THEN** the last recording file is well-formed and the FAT is uncorrupted

#### Scenario: SD filesystem consistent across critical-battery shutdown
- **WHEN** the device triggers a critical-battery auto-save and graceful shutdown
- **THEN** the last recording file is well-formed and the FAT is uncorrupted

---

### Requirement: Brand Styling Consistency
All new UI elements (status bar, recorder screen, main menu, file management, settings, dialogs) SHALL use the existing brand palette: `COL_BG` (0x1A1A1A), `COL_COPPER` (0xC78C5C), `COL_RED` (0xC75C5C), `COL_OFFWHITE` (0xEDE8E2), `COL_GREY` (0xA8A39D), `COL_SURFACE` (0x2A2A2A).

#### Scenario: New UI uses brand palette
- **WHEN** any new screen, widget, or dialog introduced by this change is rendered
- **THEN** its colours are drawn from the existing brand palette constants and no off-palette colours are introduced

