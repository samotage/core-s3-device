## 1. Planning (Phase 1)

- [x] 1.1 Create OpenSpec proposal files (proposal.md, tasks.md, spec.md)
- [x] 1.2 Validate proposal with `openspec validate recorder-standalone-experience --strict`
- [ ] 1.3 Review and get approval

## 2. Implementation (Phase 2)

### 2.A — App-Level Recording Model (FR35, NFR1, NFR2)

- [x] 2.A.1 Promote `AppRecorderModel` from page-owned to app-level scope; instantiate at `App_Init()` and tear down at `App_Uninit()` (PRD FR35, Tech §7 bullet 5).
- [x] 2.A.2 Refactor `AppRecorder` page to reference the app-level model rather than own it; remove `MicEnd()` from `onViewUnload` so recording survives page navigation (PRD FR35, FR21).
- [x] 2.A.3 Expose recording-state queries (is_recording, elapsed_seconds, current_level) to other pages and to the status bar (PRD FR5, FR8).
- [x] 2.A.4 Verify mic DMA buffer drain timing (33 ms cadence) is still met when the model runs cross-page (PRD NFR2).

### 2.B — Status Bar (FR1–FR5, NFR2)

- [x] 2.B.1 Add new status bar widget (e.g. `src/pages/_widgets/StatusBar.{cpp,h}`) rendered as a fixed top overlay on every screen (PRD FR1, NFR4).
- [x] 2.B.2 Render numeric battery percentage from AXP2101 voltage via a LiPo discharge-curve lookup table (not linear) (PRD FR2, Tech §7 bullet 1).
- [x] 2.B.3 Render charging icon when `AxpBatIsCharging()` is true (PRD FR3, Tech §7 bullet 1).
- [x] 2.B.4 Render enlarged glanceable WiFi indicator (significantly larger than the current 14 px icon) (PRD FR4).
- [x] 2.B.5 Render red recording dot when app-level recording state is active and the current page is not the recorder screen (PRD FR5, FR11).
- [x] 2.B.6 Drive status bar refresh from an app-level timer at ≥ every 5 s without disrupting mic DMA timing (PRD NFR2).
- [x] 2.B.7 Add status-bar refresh-interval constant to `include/config.h` (PRD NFR2).

### 2.C — Main Menu Replacement (FR10–FR12)

- [x] 2.C.1 Rewrite `HomeMenuModel` / `HomeMenuView` to render exactly five entries: Recorder, Files, Settings, Sleep, Power Off (PRD FR10, UI §6).
- [x] 2.C.2 Wire each entry to its target page or action via `PageManager` (PRD FR10).
- [x] 2.C.3 Remove factory demo apps (Camera, IMU, I2C, Touch, SD, RTC, Power, WiFi) from `AppFactory` menu mapping while leaving page classes registered (PRD FR11).
- [x] 2.C.4 Add a Back/Menu button on every sub-screen (Recorder, Files, Settings) that returns to the main menu (PRD FR12).
- [x] 2.C.5 Hide or disable the Sleep menu entry when recording is active (PRD FR25).

### 2.D — Recorder Screen Redesign (FR6–FR9, NFR4)

- [x] 2.D.1 Add otageLabs logo asset (verify or add under `src/res/img/`) and render it as the dominant visual element on the recorder screen (PRD FR6, NFR4).
- [x] 2.D.2 Replace the existing record control with a pill-shaped (rounded rectangle) button in `COL_COPPER` positioned below the logo (PRD FR7, NFR4).
- [x] 2.D.3 Idle state shows only logo + record button + status bar — remove any other recorder-screen widgets (PRD FR8, FR9).
- [x] 2.D.4 Recording state changes the button to a red Stop control, displays an MM:SS timer, and renders a VU level bar bound to the app-level recording model (PRD FR8).
- [x] 2.D.5 Remove SD card free-space readout from the recorder screen (PRD FR9).

### 2.E — File Management Screen (FR13–FR17, NFR3)

- [x] 2.E.1 Create new `AppFiles` page (`src/pages/AppFiles/*`) and register it in `AppFactory` (PRD FR13).
- [x] 2.E.2 Display total number of recording files on the SD card (PRD FR13).
- [x] 2.E.3 Display total storage and used storage (e.g. "1.2 / 14.8 GB") (PRD FR14).
- [x] 2.E.4 Display estimated recording time remaining computed from free space at 16 kHz mono 16-bit (~115 MB/hour) (PRD FR15).
- [x] 2.E.5 Render a "Delete oldest 10" button (PRD FR16).
- [x] 2.E.6 On press, show a confirmation dialog ("Delete 10 oldest recordings?") with Yes/No (PRD FR17, UI §6).
- [x] 2.E.7 On Yes, delete the 10 oldest recording files (sorted by filename) and refresh the on-screen stats (PRD FR16, FR17).
- [x] 2.E.8 Show a progress indicator (or at minimum prevent UI freeze) during file count / delete operations (PRD NFR3).
- [x] 2.E.9 Add a Back button returning to the main menu (PRD FR12).

### 2.F — Settings Screen (FR18)

- [x] 2.F.1 Create new `AppSettings` page (`src/pages/AppSettings/*`) and register it in `AppFactory` (PRD FR18).
- [x] 2.F.2 Render a brightness control wired to `M5.Display.setBrightness()` (PRD FR18, Tech §7 bullet 2).
- [x] 2.F.3 Persist the selected brightness across the session and use it as the default in place of the hardcoded `60` in `main.cpp` (PRD FR18, Tech §7 bullet 2).
- [x] 2.F.4 Add a Back button returning to the main menu (PRD FR12).

### 2.G — Screen Auto-Off (FR19–FR21)

- [x] 2.G.1 Add `SCREEN_IDLE_TIMEOUT_MS = 60000` to `include/config.h` (PRD FR19).
- [x] 2.G.2 Track last-touch timestamp at app-level and turn the backlight off via `M5.Display.setBrightness(0)` after `SCREEN_IDLE_TIMEOUT_MS` of inactivity (PRD FR19, Tech §7 bullet 2).
- [x] 2.G.3 On any touch input, restore backlight to the Settings-selected brightness and reset the idle timer (PRD FR20).
- [x] 2.G.4 Verify mic capture, SD write, and WAV header flushing continue uninterrupted while the screen is off (PRD FR21, NFR1).

### 2.H — Deep Sleep (FR22–FR25, NFR1)

- [x] 2.H.1 Implement a deep-sleep entry helper that calls `esp_deep_sleep_start()` (PRD FR22, Tech §7 bullet 3).
- [x] 2.H.2 Configure touch-controller (FT6336U) interrupt as a deep-sleep wake source via `esp_sleep_enable_ext0_wakeup()` (with explicit hardware-verification step) (PRD FR23, Tech §7 bullet 3).
- [x] 2.H.3 Configure AXP2101 power-key as a deep-sleep wake source (PRD FR23).
- [x] 2.H.4 Wire the "Sleep" main-menu entry to the helper (PRD FR22).
- [x] 2.H.5 Ensure on-wake boot path lands on the recorder screen within 4 seconds (PRD FR24).
- [x] 2.H.6 Disable or hide the Sleep menu entry when recording is active (PRD FR25).
- [x] 2.H.7 Verify SD filesystem is left consistent across sleep/wake (PRD NFR1).

### 2.I — Power Off (FR26–FR29, NFR1)

- [x] 2.I.1 Implement an AXP2101 hardware-shutdown helper in `AppPowerModel` (PRD FR26, Tech §7 bullet 4).
- [x] 2.I.2 Wire the "Power Off" main-menu entry to the helper behind a confirmation dialog ("Power off device?") (PRD FR27, UI §6).
- [x] 2.I.3 If recording is active when Power Off is confirmed, finalise the WAV header, flush buffered data, and close the file before shutdown (PRD FR29, NFR1).
- [x] 2.I.4 No software work required for power-on (AXP2101 power key handles in hardware) (PRD FR28, Tech §7 bullet 4).

### 2.J — Critical Battery Protection (FR30–FR32, NFR1)

- [x] 2.J.1 Sample battery voltage during recording via the AXP2101 reuse path (PRD FR30, Tech §7 bullet 1).
- [x] 2.J.2 Add critical-battery threshold constants (percent and/or voltage) to `include/config.h`, targeting ~8–10% / ~3.3 V (PRD FR31).
- [x] 2.J.3 On threshold breach during recording, auto-save the current recording: finalise WAV header, flush, close file (PRD FR31, NFR1).
- [x] 2.J.4 After auto-save, perform a graceful AXP2101 shutdown via the helper from 2.I.1 (PRD FR32, NFR1).

### 2.K — Storage Full Handling (FR33–FR34)

- [x] 2.K.1 Add `STORAGE_FULL_THRESHOLD_BYTES` (e.g. 5_000_000) to `include/config.h` (PRD FR33).
- [x] 2.K.2 Before allowing a recording to start, check SD free space against the threshold; below it, disable the record button (PRD FR33).
- [x] 2.K.3 Render a "storage full" message explaining why recording is blocked, with a control that navigates to the File Management screen (PRD FR33, FR34).

## 3. Testing (Phase 3)

- [x] 3.1 Build clean for `pio run` on the target environment with no warnings introduced.
- [x] 3.2 Native unit tests (Unity) for LiPo voltage → percentage conversion table (PRD FR2).
- [x] 3.3 Native unit tests for estimated-recording-time math given free-space + 115 MB/hour rate (PRD FR15).
- [x] 3.4 Native unit tests for "oldest 10 by filename sort" selection logic given a representative file list (PRD FR16).
- [x] 3.5 Native unit test for storage-full predicate against `STORAGE_FULL_THRESHOLD_BYTES` (PRD FR33).
- [x] 3.6 Native unit test for critical-battery predicate at the configured percent / voltage threshold (PRD FR31).
- [ ] 3.7 On-device manual: status bar renders battery %, charging, WiFi, and recording dot on every screen (PRD FR1–FR5, SC1–SC2, SC11).
- [ ] 3.8 On-device manual: recorder screen idle vs recording states match the UI mock (logo hero, pill button, timer, VU bar) (PRD FR6–FR8, SC12).
- [ ] 3.9 On-device manual: navigation Recorder ↔ Files ↔ Settings ↔ Menu with back buttons; recording continues across navigation (PRD FR10–FR12, FR35, SC10).
- [ ] 3.10 On-device manual: File Management shows file count, storage stats, estimated time; "Delete oldest 10" requires confirmation, then removes the right 10 (PRD FR13–FR17, SC6–SC7).
- [ ] 3.11 On-device manual: brightness slider in Settings drives backlight (PRD FR18).
- [ ] 3.12 On-device manual: screen turns off after ~1 minute idle, any touch wakes it, recording continues (PRD FR19–FR21, SC3).
- [ ] 3.13 On-device manual: deep sleep from menu, wake via touch and via power button, reach recorder screen in < 4 s; Sleep is disabled while recording (PRD FR22–FR25, SC4).
- [ ] 3.14 On-device manual: Power Off from menu with confirmation; power on via physical button; auto-save fires if recording (PRD FR26–FR29, SC5).
- [ ] 3.15 On-device manual: critical-battery auto-save and graceful shutdown on a low LiPo (or bench-spoofed voltage) (PRD FR30–FR32, SC9, NFR1).
- [ ] 3.16 On-device manual: storage-full message and disabled record button when SD is below threshold; navigation to File Management offered (PRD FR33–FR34, SC8).
- [ ] 3.17 SD card mount + read back of WAV files written across screen-off, deep sleep, power off, and critical-battery shutdown — no FAT corruption, no truncated files beyond a single trailing buffer (PRD NFR1).
- [ ] 3.18 Performance check: status-bar refresh does not produce mic dropouts or DMA overruns at 33 ms cadence (PRD NFR2).
- [ ] 3.19 UI responsiveness check: file count and delete-oldest-10 do not freeze the display (PRD NFR3).

## 4. Final Verification

- [x] 4.1 `pio run` succeeds with no new warnings.
- [x] 4.2 All native Unity tests pass (`pio test -e native`).
- [ ] 4.3 All on-device manual checks above signed off.
- [x] 4.4 Brand styling matches existing palette (`COL_BG`, `COL_COPPER`, `COL_RED`, `COL_OFFWHITE`, `COL_GREY`, `COL_SURFACE`) (PRD NFR4).
- [x] 4.5 Confirm factory demo pages remain compiled-in but unreachable from navigation (PRD FR11).
- [x] 4.6 Confirm out-of-scope items are not implemented (cloud upload, NTP, playback, file detail, transcription, BLE, LED, configurable sleep timeout in UI, mic gain in UI).

## Pre-Build Contracts — "Verify By" criteria

- 2.A.1 Verify by: `g_app_recorder_model` global declared in `App.h`; instantiated in `App_Init`; `AppRecorder` page references it via pointer; firmware compiles.
- 2.A.2 Verify by: `AppRecorder::onViewUnload()` no longer calls `MicEnd()`; navigation away from recorder while recording leaves `IsRecording()` true.
- 2.A.3 Verify by: `IsRecording()`, `RecordingSeconds()`, `InputLevel()` accessible from the status bar widget and other pages via the shared model.
- 2.A.4 Verify by: existing 33ms timer cadence retained on the recorder page; no new long-running work added to it.
- 2.B.1 Verify by: `StatusBar.{cpp,h}` exists in `src/pages/_widgets/`; offered on each page via a single `Attach(parent)` call; compiles.
- 2.B.2 Verify by: LiPo lookup table in `StatusBar.cpp`; unit test under `test/test_native/` covers boundary points.
- 2.B.3 Verify by: charging icon toggled by `AxpBatIsCharging()` return value at refresh tick.
- 2.B.4 Verify by: WiFi icon at >= 24px height (vs old 14px).
- 2.B.5 Verify by: red dot only rendered when `g_app_recorder_model->IsRecording()` true AND the page is not `AppRecorder` (status bar consults a `setRecorderPage(bool)` flag set by AppRecorder lifecycle hooks).
- 2.B.6 Verify by: lv_timer at 5000ms in the StatusBar; no calls into the mic path from this timer.
- 2.B.7 Verify by: `STATUS_BAR_REFRESH_MS` define in `include/config.h`.
- 2.C.1 Verify by: `HomeMenuView::ui` exposes 5 buttons labelled Recorder, Files, Settings, Sleep, Power Off.
- 2.C.2 Verify by: each click routes via `PageManager::Replace()` to the named page (or executes sleep/power-off helper).
- 2.C.3 Verify by: `AppFactory.cpp` still includes/registers demo pages, but `HomeMenu` no longer creates buttons for them.
- 2.C.4 Verify by: every sub-page renders a back button (Menu) and clicking it navigates to `Pages/HomeMenu`.
- 2.C.5 Verify by: Sleep button is hidden / disabled when `g_app_recorder_model->IsRecording()` is true.
- 2.D.1 Verify by: logo image asset (`otagelabs_logo`) is rendered as the hero element on recorder screen.
- 2.D.2 Verify by: record button is a rounded rectangle with radius >= 24px in COL_COPPER, sized as a pill (~180x52).
- 2.D.3 Verify by: idle state has only status bar + logo + button visible.
- 2.D.4 Verify by: recording state shows MM:SS timer + VU level bar; button bg becomes COL_RED, label "Stop".
- 2.D.5 Verify by: no `Tap to record - X GB free` text on recorder; no `free_mb` parameter shown.
- 2.E.1 Verify by: `src/pages/AppFiles/{AppFiles,AppFilesView,AppFilesModel}.{cpp,h}` exist and `AppFactory.cpp` registers `AppFiles`.
- 2.E.2-4 Verify by: AppFiles view shows file count, used/total storage, est. time.
- 2.E.5-7 Verify by: button + msgbox-confirmation flow performs delete and refreshes stats.
- 2.E.8 Verify by: file count happens once on page load, not per frame; delete shows "Deleting…" overlay during operation.
- 2.E.9 Verify by: Back button navigates to `Pages/HomeMenu`.
- 2.F.1-4 Verify by: `src/pages/AppSettings/{AppSettings,AppSettingsView}.{cpp,h}` exist; brightness slider drives `M5.Display.setBrightness()`; Back button to HomeMenu; selected brightness used by app at boot.
- 2.G.1-3 Verify by: `SCREEN_IDLE_TIMEOUT_MS` in config.h; app-level idle tracker turns backlight off after timeout; touch wakes via LVGL touch read hook.
- 2.G.4 Verify by: backlight changes do not call into Mic/SD modules.
- 2.H.1-5 Verify by: helper in AppPowerModel calls `esp_deep_sleep_start()` after enabling wake sources (touch ext1 + power-key ext1); wired to Sleep entry; AppRecorder is the boot landing page.
- 2.H.6 Verify by: Sleep entry blocked when recording.
- 2.H.7 Verify by: recorder model `MicEnd()` + SD close performed before deep sleep entry.
- 2.I.1-3 Verify by: AXP2101 power-off helper writes the shutdown bit; wired to PowerOff entry behind msgbox; auto-saves recording first.
- 2.J.1-4 Verify by: critical-battery predicate in AppPowerModel; called from recorder loop; auto-saves and shuts down on threshold breach.
- 2.K.1-3 Verify by: `STORAGE_FULL_THRESHOLD_BYTES` in config.h; record button disabled when below threshold; storage-full status message shown.
