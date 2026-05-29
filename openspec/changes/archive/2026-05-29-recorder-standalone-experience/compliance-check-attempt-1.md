# PRD Compliance Check — recorder-standalone-experience (Attempt 1)

- **Date:** 2026-05-29
- **PRD:** `docs/prds/recorder-standalone-experience-prd.md`
- **Branch:** `feature/recorder-standalone-experience`
- **Reviewer:** orchestration compliance worker

## Verdict

**COMPLIANT**

All 35 functional requirements (FR1–FR35) and 4 non-functional requirements (NFR1–NFR4) are addressed in code. Firmware compiles cleanly for the M5Stack CoreS3 target. All 18 native Unity tests pass. OpenSpec proposal, tasks, and spec align with the PRD; no scope creep detected.

The on-device manual checks (tasks 3.7–3.19, 4.3) remain unchecked in `tasks.md` — these require physical hardware verification and are appropriately deferred. The code paths they test are present and architecturally correct.

## Compilation

```
pio run -e m5stack-cores3
RAM:   [===       ]  32.9% (used 107948 / 327680 bytes)
Flash: [========= ]  93.3% (used 6848069 / 7340032 bytes)
SUCCESS — 00:00:04.525
```

Native env failure under bare `pio run` is benign (build sweep tries to compile firmware sources against the native toolchain). The `m5stack-cores3` env, the only firmware target, builds clean.

## Native Tests

```
pio test -e native
18 test cases: 18 succeeded in 00:00:00.325
```

Covers FR2 (LiPo curve), FR15 (estimated minutes math), FR16 (oldest-N selection),
FR31 (critical battery predicate), FR33 (storage-full predicate).

## Requirements Scorecard

### Status Bar
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR1 | Persistent status bar on every screen | PASS | `src/pages/_widgets/StatusBar.{cpp,h}` attached in HomeMenuView, AppRecorderView, AppFilesView, AppSettings |
| FR2 | Battery % from AXP2101 voltage (LiPo curve) | PASS | `RecorderMath::BatteryPercentFromMv` (8 anchor points, linear interp); `StatusBar::Refresh` calls it |
| FR3 | Charging icon from charge status reg | PASS | `StatusBar.cpp:94-99` toggles via `AxpBatIsCharging()` |
| FR4 | Glanceable WiFi indicator (larger than 14px) | PASS | `StatusBar.cpp:50` uses `lv_font_montserrat_20` (vs old 14) |
| FR5 | Red dot on non-recorder screens when recording | PASS | `StatusBar.cpp:106-111` guards on `on_recorder_page_` flag; recorder calls `SetOnRecorderPage(true)` |
| NFR2 | Status bar refresh ≥ 5s, no DMA disruption | PASS | `STATUS_BAR_REFRESH_MS=5000`; runs from `lv_timer`, not mic path |

### Recorder Screen
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR6 | otageLabs logo as dominant element | PASS | Asset `src/res/img/otagelabs_logo.c`; declared + rendered in `AppRecorderView.cpp:13,28-31` |
| FR7 | Pill-shaped record button in copper accent | PASS | `AppRecorderView.cpp:53-60` — 180x48 size, radius=24, `COL_COPPER` |
| FR8 | Recording state: stop control + timer + VU bar | PASS | `SetRecording()` toggles colour to `COL_RED`, label "Stop", shows MM:SS timer + VU bar `bar_level` |
| FR9 | No SD free-space readout on recorder | PASS | No free_mb display; only storage-full message when blocked |

### Main Menu
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR10 | 5 entries: Recorder, Files, Settings, Sleep, Power Off | PASS | `HomeMenuView.cpp:45-49` creates exactly 5 buttons |
| FR11 | Factory demos unwired from nav (still compiled) | PASS | `AppFactory.cpp` still registers all demo pages; `HomeMenu` does not reference them |
| FR12 | Back button on every sub-screen | PASS | AppRecorder, AppFiles, AppSettings all create `btn_menu` returning to `Pages/HomeMenu` |

### File Management
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR13 | Total file count | PASS | `AppFilesModel::CollectStats` counts `REC_*.wav`; view shows "Files: N" |
| FR14 | Total/used storage | PASS | `SD.totalBytes()` and `SD.usedBytes()`; view formats as "X.X / Y.Y GB" |
| FR15 | Estimated time at 115 MB/hr | PASS | `RecorderMath::EstimatedMinutes` (32000 bytes/sec × 3600 = 115.2 MB/hr) |
| FR16 | Delete oldest 10 by filename sort | PASS | `DeleteOldestRecordings` uses qsort + strcmp + caps at min(count, n) |
| FR17 | Confirmation dialog ("Delete 10 oldest recordings?") | PASS | `AppFiles.cpp:67` `lv_msgbox_create` with Yes/No; only Yes triggers delete |
| NFR3 | UI responsive during SD ops | PASS | `SetBusy(true)` spinner + `lv_refr_now()` before blocking SD walk |

### Settings
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR18 | Brightness control via setBrightness() | PASS | `AppSettings.cpp:81` slider drives `M5.Display.setBrightness(g_app_brightness)`; persisted across session; used at boot via `main.cpp:54` |

### Screen Auto-Off
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR19 | Backlight off after 1 min idle | PASS | `App.cpp:15-29` `screen_idle_tick` polls `lv_disp_get_inactive_time()` every 200ms; off at `SCREEN_IDLE_TIMEOUT_MS=60000` |
| FR20 | Touch wakes screen, resets timer | PASS | LVGL touch input auto-resets `inactive_time`; tick restores brightness to `GetCurrentBrightness()` |
| FR21 | Recording uninterrupted during screen-off | PASS | Tick only calls `setBrightness()`; no mic/SD calls |

### Deep Sleep
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR22 | Sleep → `esp_deep_sleep_start()` | PASS | `AppPowerModel::DeepSleep()` calls it |
| FR23 | Wake via touch OR power key | PARTIAL-OK | Power-key wake via `ext0_wakeup(GPIO_NUM_0, 0)` enabled; touch wake explicitly deferred pending hardware verify, with comment matching PRD language "subject to hardware verification". PRD permits this. |
| FR24 | Boot to recorder in < 4s | PASS (architecturally) | `App.cpp:103` boot lands directly on `Pages/AppRecorder` (no StartUp splash). 4s budget not empirically measured. |
| FR25 | Sleep hidden when recording | PASS | `HomeMenu::onViewLoad` + `Update` call `SetSleepEnabled(!recording)`; view hides button via `LV_OBJ_FLAG_HIDDEN` |

### Power Off
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR26 | AXP2101 shutdown helper | PASS | `AppPowerModel::PowerOff()` writes reg `0x10` bit 0 |
| FR27 | Confirmation dialog | PASS | `HomeMenu` `showConfirm` shows "Power off device?" msgbox |
| FR28 | Power-on via physical button | PASS (hardware) | AXP2101 handles; no software work needed |
| FR29 | Auto-save recording before shutdown | PASS | `HomeMenu.cpp:78-80` calls `StopRecording()` before `pm.PowerOff()` |

### Critical Battery
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR30 | Battery voltage monitored during recording | PASS | `AppRecorder::Update` polls `pm.IsCriticalBattery()` every 5s |
| FR31 | Auto-save at critical threshold | PASS | `RecorderMath::IsCriticalBattery` predicate (mv ≤ 3300 OR pct ≤ 8); `StopRecording()` finalises WAV |
| FR32 | Graceful AXP shutdown after auto-save | PASS | `AppRecorder.cpp:142-144` `StopRecording()` then `pm.PowerOff()` |

### Storage Full
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR33 | Block recording when SD < threshold | PASS | `AppRecorder::StorageFull()` checks vs `STORAGE_FULL_THRESHOLD_BYTES=5_000_000`; record button disabled; `SetStorageFull()` view state |
| FR34 | Nav to File Management offered | PASS | `btn_to_files` in recorder view routes to `Pages/AppFiles` |

### Recording Architecture
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR35 | App-level recording model | PASS | `g_app_recorder_model` singleton in `App.cpp:36-37`; `AppRecorder::onViewUnload()` no longer calls `MicEnd()` (comment confirms); pages reference via pointer |

### Non-Functional
| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| NFR1 | SD consistent across power transitions | PASS | WAV header periodic flush + `StopRecording()` before sleep/poweroff/critical |
| NFR2 | Status bar refresh ≥ 5s, no DMA impact | PASS | See FR1/status bar table |
| NFR3 | UI responsive during SD ops | PASS | See FR13/file mgmt table |
| NFR4 | Brand palette consistency | PASS | All new views use `COL_BG/COL_COPPER/COL_RED/COL_OFFWHITE/COL_GREY/COL_SURFACE` defined locally per file from the same hex values |

## Failures and Violations

None.

## Partial Implementations

- **FR23 touch wake** is intentionally deferred (only power-key wake is enabled). The PRD language ("subject to hardware verification") permits this. Code comment matches PRD intent. Reviewer accepts as compliant with the stated PRD scope.
- **On-device manual tests (3.7–3.19)** remain unticked in tasks.md. These require physical hardware and are listed as future verification by the build agent; not a compliance failure for the implementation review.

## Scope Creep

None detected. All 32 changed files trace to PRD requirements:

- Source code: all new pages (`AppFiles/*`, `AppSettings/*`, `_widgets/StatusBar.*`) and modified files (`App.cpp`, `main.cpp`, `AppRecorder/*`, `HomeMenu/*`, `AppPower/AppPowerModel.*`, `AppFactory.cpp`) map directly to FR/NFR items.
- `include/config.h` adds only the constants enumerated in the proposal.
- `include/recorder_math.h` adds pure-math helpers used by both firmware and tests — supporting infrastructure for FR2/FR15/FR16/FR31/FR33.
- `include/lv_conf.h` enables additional Montserrat font sizes (16, 20, 22) used by the new UI — supporting infrastructure.
- `platformio.ini` adds a `native` test env — supporting infrastructure for FR2/FR15/FR16/FR31/FR33 unit tests.
- `test/test_native/test_recorder_math.cpp` — test code for PRD-mandated unit tests (tasks 3.2–3.6).
- `src/res/img/otagelabs_logo.c` — required asset for FR6.

## Compilation Status

`pio run -e m5stack-cores3` — **SUCCESS** (RAM 32.9%, Flash 93.3%).

## OpenSpec Alignment

- **proposal.md** — Why/What Changes sections fully cover FR1–FR35 and NFR1–NFR4. Affected code list matches actual files modified. BREAKING markers correctly applied to menu rewrite, recorder screen redesign, and recording model lift.
- **tasks.md** — Every implementation task (2.A.1–2.K.3) traces to PRD FR/NFR ids. Pre-build "Verify by:" contracts match observed implementation.
- **spec.md** — All 12 ADDED requirements correspond to PRD sections. Each requirement has scenarios that map to PRD acceptance criteria and SCs.

No orphan tasks. No reinterpreted requirements. No unimplemented scope.
