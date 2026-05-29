# Smoke Test Report — recorder-standalone-experience

**Date:** 2026-05-29 14:38 AEST
**Branch:** feature/recorder-standalone-experience
**Compile status:** SUCCESS (m5stack-cores3)
**Unit tests:** 18/18 PASSED (native)

## Compilation

| Environment | Status | Duration | Warnings |
|---|---|---|---|
| m5stack-cores3 | SUCCESS | 5.79 s | 0 |
| native (test_native) | PASSED | 0.38 s | 0 |

Note: `pio run` (no env) also iterates `native` env in compile-only mode and fails because Unity tests reference `Arduino.h` from production sources. This is a long-standing platformio.ini structure (`test_build_src = no` in native), expected behaviour, and not caused by this change. The supported invocations — `pio run -e m5stack-cores3` and `pio test -e native` — both succeed cleanly.

## Resource Budget

| Resource | Used | Total | Pct | Status |
|---|---|---|---|---|
| Flash (app partition) | 6,848,213 B | 7,340,032 B (7 MB) | 93.3% | MAJOR — over 85% threshold |
| RAM (SRAM) | 107,956 B | 327,680 B | 32.9% | OK |

The `partitions_ffat.csv` allocates 7 MB to the factory app partition; flash usage at 93.3% leaves ~480 KB headroom. No immediate failure, but headroom is thin for any future additions. Not a launch blocker — pre-existing trend (LVGL + M5GFX + esp32-camera dominate the image).

## Checks Executed

| # | Check | Target | Result |
|---|---|---|---|
| 1 | Compilation clean (target env) | `pio run -e m5stack-cores3` | PASS |
| 2 | Unit tests pass | `pio test -e native` | PASS (18/18) |
| 3 | Resource budget — RAM | 32.9% | PASS |
| 4 | Resource budget — Flash | 93.3% (app partition) | MAJOR (warning, not blocking) |
| 5 | Include chain — new headers reachable | StatusBar, AppFiles, AppSettings, recorder_math | PASS |
| 6 | Page registration — AppFactory | `APP_CLASS_MATCH(AppFiles)`, `APP_CLASS_MATCH(AppSettings)` | PASS |
| 7 | Page installation — App.cpp | `Install("AppFiles", ...)`, `Install("AppSettings", ...)` | PASS |
| 8 | HomeMenu wires new pages | `Replace("Pages/AppFiles")`, `Replace("Pages/AppSettings")` | PASS |
| 9 | App-level recorder model lifecycle | Set in `App_Init`, cleared in `App_Uninit` | PASS |
| 10 | Recorder model API surface | `IsRecording()`, `RecordingSeconds()`, `InputLevel()` | PASS |
| 11 | StatusBar attached to all target screens | AppRecorder, AppFiles, AppSettings, HomeMenu | PASS |
| 12 | StatusBar recording-page flag wired | `SetOnRecorderPage(true)` in AppRecorderView | PASS |
| 13 | Config constants present | `SCREEN_IDLE_TIMEOUT_MS`, `STATUS_BAR_REFRESH_MS`, `STORAGE_FULL_THRESHOLD_BYTES`, `CRITICAL_BATTERY_PERCENT`, `CRITICAL_BATTERY_VOLTAGE_MV` | PASS |
| 14 | PowerOff helper present | `AppPowerModel::PowerOff()` writes AXP2101 reg 0x10 bit 0 | PASS |
| 15 | DeepSleep helper present | `AppPowerModel::DeepSleep()` enables ext0 wake + `esp_deep_sleep_start()` | PASS |
| 16 | Boot landing page is recorder (SC4) | `manager.Push("Pages/AppRecorder")` in `App_Init` | PASS |
| 17 | AppFiles confirmation dialog | `lv_msgbox_create` with "Delete 10 oldest recordings?" + Yes/No buttons | PASS |
| 18 | DeleteOldestRecordings selects by filename sort | qsort-based selection in AppFilesModel | PASS |
| 19 | No new warnings | `pio run` warning grep returned empty | PASS |
| 20 | State machine integrity | No enum-driven switches in changed code (boolean flags only) | PASS (N/A) |

## Failures

### MAJOR

1. **Flash usage 93.3%** — target: < 85%. Actual: 93.3% (6,848,213 / 7,340,032 B). Leaves ~480 KB headroom on the 7 MB factory partition. Not a blocker for this PR — the trend is pre-existing (LVGL, M5GFX, esp32-camera dominate) — but a fast follower should track this. Options if it grows: drop unused demo pages from build, switch to OTA partition layout, or move to a larger app partition.

### CRITICAL / MINOR

None.

## Integration Smoke Findings

- **Function signature audit:** new public methods (`IsRecording`, `RecordingSeconds`, `InputLevel`, `SetOnRecorderPage`, `PowerOff`, `DeepSleep`, `DeleteOldestRecordings`, `LipoPercentFromMv`, `EstimatedMinutesRemaining`, `IsStorageFull`, `IsCriticalBattery`) all have implementations matching declarations and at least one call site.
- **Wake sources:** Deep sleep enables AXP power-key (GPIO0 ext0) only. Touch wake (FT6336U) is documented as deferred pending hardware verification — acceptable per PRD FR23 ("if hardware supports it"). Power button alone satisfies the wake requirement.
- **ISR safety:** No new ISRs registered in this change. No `IRAM_ATTR` usage required.

## Verdict

**PASS with one MAJOR warning.**

Compilation, unit tests, include chain, page registration, model lifecycle, and configuration constants all check out. The only finding is flash partition usage at 93.3% — not introduced by this PR but worth tracking. No critical or blocking issues. Code is ready for on-device manual verification (tasks 3.7–3.19) per PRD § 3.

## Addendum — Flash Threshold Reconciliation

**Date:** 2026-05-29 14:45 AEST
**Reconciled by:** fix agent (post-smoke)

### Master baseline (pre-PR)

Built `master` at `26d0590` with `pio run -e m5stack-cores3`:

- **Flash: 94.7% (used 6,949,297 bytes from 7,340,032 bytes)**
- RAM: 32.3% (105,748 bytes)

### This PR's delta

| Resource | master (26d0590) | feature branch | Delta |
|---|---|---|---|
| Flash | 6,949,297 B (94.7%) | 6,848,213 B (93.3%) | **−101,084 B (−1.4 pp)** |
| RAM | 105,748 B (32.3%) | 107,956 B (32.9%) | +2,208 B (+0.6 pp) |

This PR **reduces** flash usage by ~101 KB compared to master. The simplify pass (`fff77ca`) and the move from page-scoped to app-scoped recorder model net out as a flash *win*, not a regression.

### PRD constraint check

The PRD at `docs/prds/recorder-standalone-experience-prd.md` was reviewed for flash/memory budgets across §2 (Scope), §3 (Success Criteria), §4 (FRs), §5 (NFRs), §6 (UI Overview), and §7 (Technical Context). **No flash budget is specified.** The only memory-adjacent NFR (NFR2) concerns mic DMA buffer drain timing, not partition usage.

### Conclusion

The smoke worker's 85% flash threshold is an internal default heuristic, not a PRD requirement. Master itself sits at 94.7% — the 93.3% figure on this branch is a **strict improvement** against the pre-PR baseline. Failing this PR against an 85% threshold that master also fails would be a tooling artefact, not a real regression.

**Decision:** No code trimming performed. The flash trend (LVGL + M5GFX + esp32-camera footprint) is pre-existing and acknowledged as a separate tracking item per §"Failures → MAJOR" above. This PR is cleared on the flash dimension.

## Retest Results

**Date:** 2026-05-29 14:43 AEST
**Branch:** feature/recorder-standalone-experience @ d2fc2cf
**Attempt:** 2 (post-reconciliation retest)

### Verdict

**PASS — all checks cleared. No CRITICAL or MAJOR blockers.**

### Compilation

| Environment | Status | Duration | Warnings |
|---|---|---|---|
| m5stack-cores3 | SUCCESS | 4.72 s | 0 |
| native (test_native) | PASSED | 1.09 s | 0 |

### Resource Budget (re-measured)

| Resource | Used | Total | Pct | Status |
|---|---|---|---|---|
| Flash (app partition) | 6,848,213 B | 7,340,032 B (7 MB) | 93.3% | INFO (reconciled — see Addendum) |
| RAM (SRAM) | 107,956 B | 327,680 B | 32.9% | OK |

Flash check reclassified from MAJOR → **INFO** per the Addendum reconciliation: PRD imposes no flash budget, master baseline is 94.7%, and this branch reduces flash by ~101 KB. The 85% threshold is a worker-internal default heuristic, not a PRD requirement.

### Checks Re-Verified

| # | Check | Target | Result |
|---|---|---|---|
| 1 | Compilation clean (target env) | `pio run -e m5stack-cores3` | PASS |
| 2 | Unit tests pass | `pio test -e native` | PASS (18/18) |
| 3 | Resource budget — RAM | 32.9% | PASS |
| 4 | Resource budget — Flash | 93.3% (no PRD budget; -101 KB vs master) | INFO (reconciled) |
| 5 | Include chain — new headers reachable | StatusBar, AppFiles, AppSettings, recorder_math | PASS |
| 6 | Page registration — AppFactory | `APP_CLASS_MATCH(AppFiles)`, `APP_CLASS_MATCH(AppSettings)` at `src/pages/AppFactory.cpp:62-63` | PASS |
| 7 | Page installation — App.cpp | `Install("AppFiles", ...)`, `Install("AppSettings", ...)` at `src/App.cpp:97-98` | PASS |
| 8 | HomeMenu wires new pages | `Replace("Pages/AppFiles")`, `Replace("Pages/AppSettings")` at `src/pages/HomeMenu/HomeMenu.cpp:115,117` | PASS |
| 9 | App-level recorder model lifecycle | Set in `App_Init` (`src/App.cpp:36-37`), cleared in `App_Uninit` (`src/App.cpp:110`) | PASS |
| 10 | Recorder model API surface | `IsRecording()`, `RecordingSeconds()`, `InputLevel()` in `AppRecorderModel.h:54-57` | PASS |
| 11 | StatusBar attached to all target screens | AppRecorder, AppFiles, AppSettings, HomeMenu | PASS |
| 12 | StatusBar recording-page flag wired | `SetOnRecorderPage(true)` in `AppRecorderView.cpp:25` | PASS |
| 13 | Config constants present | `SCREEN_IDLE_TIMEOUT_MS`, `STATUS_BAR_REFRESH_MS`, `STORAGE_FULL_THRESHOLD_BYTES`, `CRITICAL_BATTERY_PERCENT`, `CRITICAL_BATTERY_VOLTAGE_MV` in `include/config.h:41-53` | PASS |
| 14 | PowerOff helper present | `AppPowerModel::PowerOff()` writes AXP2101 reg 0x10 bit 0 (`AppPowerModel.cpp:119-123`) | PASS |
| 15 | DeepSleep helper present | `AppPowerModel::DeepSleep()` enables ext0 wake + `esp_deep_sleep_start()` (`AppPowerModel.cpp:127-135`) | PASS |
| 16 | Boot landing page is recorder (SC4) | `manager.Push("Pages/AppRecorder")` at `App.cpp:103` | PASS |
| 17 | AppFiles confirmation dialog | `lv_msgbox_create` with "Delete 10 oldest recordings?" at `AppFiles.cpp:74-75` | PASS |
| 18 | DeleteOldestRecordings selects by filename sort | qsort-based selection in `AppFilesModel.cpp:88-90` | PASS |
| 19 | No new warnings | `pio run` warning grep returned empty | PASS |
| 20 | State machine integrity | No enum-driven switches in changed code (boolean flags only) | PASS (N/A) |

### Failures

**None.** No CRITICAL, no MAJOR. The previously-flagged flash usage is reconciled to INFO per addendum.

### Notes

- Branch state is unchanged since the original smoke run; the fix-agent pass only added the Addendum reconciliation section to this report. No code was modified.
- All 20 checks pass. The previous run's sole MAJOR (flash > 85%) is the worker-internal default heuristic; PRD has no flash budget, and the branch is a strict improvement (−1.4 pp / −101 KB) vs the `master` baseline at 94.7%.
- Cleared for compliance and downstream phases. On-device manual verification (PRD § 3 SC1–SC10, tasks 3.7–3.19) remains as a separate operator step.
