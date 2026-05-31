---
validation:
  status: valid
  validated_at: '2026-05-31T16:28:19+10:00'
---

## Product Requirements Document (PRD) — SD Write Architecture Remediation

**Project:** CoreS3 Meeting Recorder
**Scope:** Re-architect the audio→SD write path to eliminate mid-recording cut-offs and hard SD-card wedges, using a PSRAM ring buffer, a dedicated SD-writer task, and an SPI-bus mutex.
**Author:** Robbo (workshopped with Sam and Chip, #workshop-recorder-remediation-305)
**Date:** 2026-05-31
**Status:** Draft
**Type:** Remediation

---

## Executive Summary

The standalone recorder cuts off mid-meeting at unpredictable times and, on failure, the SD card *hard-wedges* — every subsequent write returns 0 bytes and only a full power cycle (battery hold-6s + USB) recovers it. The user loses the entire remainder of the meeting. Two real recordings were lost on the bench on 2026-05-31.

The root cause is confirmed by bench bisection: the capture path writes tiny 2 KB chunks **synchronously, directly from the LVGL/UI thread**, with no buffer beyond the I2S DMA (~tens of ms). SD cards are non-deterministic — a card routinely stalls **up to ~1.6 s** mid-write doing internal wear-levelling. When the card stalls, the single UI thread blocks, the mic DMA overflows, and on the **unlocked shared SPI bus** (LCD and SD share it; the arduino-esp32 SD interface is non-locking — issue #11310) the collision cascades into the wedge. The random failure timing is simply the randomness of when the card runs garbage collection.

This is a solved problem in the ESP32 continuous-recording community. The fix is architectural: a **PSRAM ring buffer** decouples capture from SD, a **dedicated FreeRTOS SD-writer task** drains it in large blocks so a stall never blocks capture or the UI, and an **application-level SPI-bus mutex** serialises the only two bus clients (LCD and SD). Because SD writes leave the UI thread, the live elapsed-time ticker returns for free.

This PRD also **removes** the failed workarounds added while chasing this bug — the LCD-flush-suppression hack, the short-write retry loop, the SD-short-write rollover trigger, and the destructive `format_if_empty=true` auto-format that wiped the user's recordings during recovery.

---

## 1. Context & Purpose

### 1.1 The Defect

- **Symptom:** Recording stops mid-meeting at unpredictable times — observed failures at 31 s, 2.4 min, 6.5 min, and 10.4 min, with occasional clean runs to ~10 min.
- **Failure mode:** On failure the SD card hard-wedges — every write returns 0 bytes. An EN-reset does **not** clear it; only a full power cycle does.
- **User impact:** The entire remainder of the meeting is lost. This is the single most important thing a meeting recorder must never do.

### 1.2 Confirmed Root Cause

SD writes are non-deterministic — a card stalls up to ~1.6 s mid-write doing internal wear-levelling ([ESP32 forum t=24967](https://esp32.com/viewtopic.php?t=24967)). The current capture path (`AppRecorderModel::WriteChunk()`) writes 2 KB chunks synchronously from the LVGL timer thread, buffered only by the I2S DMA. When the card stalls:

1. The UI thread blocks on `wav_file.write()`.
2. The mic I2S DMA ring (tens of ms deep) overflows — samples are lost.
3. On the unlocked shared SPI bus, an LCD access interleaved with the stalled SD write corrupts state (the LCD reuses MISO as D/C — see `AppRecorderModel.cpp:8`), driving the card into the wedge.

The random failure timing equals the randomness of when the card runs GC.

### 1.3 Evidence — What Was Ruled Out (bench bisection, serial-instrumented)

- **Card:** a good SanDisk DVR card wrote 20 MB clean at all SPI speeds (25→4 MHz) in isolation.
- **SPI clock, WAV-header seeks, heap (flat throughout), mic-alone (8.7 min clean), WiFi:** all cleared.
- **LCD bus contention alone:** suppressing the LCD flush during recording still failed in **31 s** — proving the dominant problem is the unbuffered synchronous-write architecture, not the bus alone. (The bus mutex is still required to make the writer-task design safe; it is necessary but not sufficient on its own.)

### 1.4 Why Now

The recorder is otageLabs' standalone meeting device and brand showcase. A recorder that silently loses meetings is unusable. The fix is well-understood and proven; the remediation is bounded.

---

## 2. Scope

### 2.1 In Scope

- PSRAM ring buffer decoupling capture from SD.
- Dedicated FreeRTOS SD-writer task draining the ring in large blocks.
- Application-level SPI-bus mutex shared by the LCD flush and the SD writer.
- WAV header lifecycle (placeholder, periodic power-loss rewrite, stop-finalise) moved entirely into the writer task.
- Live elapsed-time ticker ≥1 Hz during recording.
- Overrun handling: stop-with-error, finalise to the failure point, surface a visible fault.
- SD recovery on short-write: single best-effort `SD.end()/begin()` retry, else stop-with-error.
- Removal of the failed workarounds (see §7 Kill List), including the destructive auto-format.

### 2.2 Out of Scope / Deferred

- **VU meter restoration** — deferred. Every LCD redraw now contends for the bus mutex; the ticker is the must-have, the VU meter is reintroduced (at a low rate) in a follow-up once stability is proven.
- **Resume-to-new-segment after SD recovery** — deferred. The first build stops with error on a confirmed wedge; segment-resume is earned only if a transient, software-recoverable wedge is ever observed.
- **Mic-codec auto-rollover** — retained as-is (separate defect). Only its *SD-short-write* trigger is removed; the *mic-fail* trigger stays.
- Audio format/codec/compression changes — format stays 16 kHz / 16-bit / mono PCM (Parakeet-native, no resampling).
- Any new UI screens or navigation changes.

---

## 3. Key Decisions

Each decision records the call, the rationale, and the rejected alternative.

- **D1 — Ring buffer: 1 MB in PSRAM (~30 s of audio).**
  Rationale: at 32 KB/s, a 1.6 s worst-case stall consumes ~51 KB (~5% of the ring). 30 s of absorption means any overrun is a genuinely dead card, not a hiccup. CoreS3 has 8 MB PSRAM with ~8.2 MB free, so 1 MB is ~12% of free PSRAM.
  Rejected: 2 MB (60 s) — buys no real safety (overrun already means card-dead) and just delays an inevitable stop.

- **D2 — Write block size: 32 KB.**
  Rationale: drains the ring in large, efficient blocks; bounds the per-write bus-hold so the LCD's mutex wait stays short. Drain when ≥1 block is buffered.
  Rejected: 64 KB — only if throughput tests show it matters; longer bus-hold otherwise penalises the ticker.

- **D3 — Overrun behaviour: stop cleanly, finalise the WAV to the failure point, surface a visible error. Never silently lose audio.**
  Rationale: a 30 s ring overrun means the card has stalled >30 s straight — the card is dead, not stalling. Everything captured so far is preserved and the file is playable. (Confirmed by Sam, 2026-05-31.)
  Rejected: drop-oldest — unacceptable for a meeting recorder; silently corrupts the record.

- **D4 — SD recovery on short-write: single best-effort `SD.end()/begin()` retry; if the retry still short-writes, stop + finalise + surface "SD fault — restart device."**
  Rationale: today's wedge needed a full power cycle (EN-reset didn't clear it), so software re-init will likely fail — but a single cheap attempt costs nothing and covers a transient case. No elaborate recovery subsystem, no resume-to-new-segment (deferred).
  Rejected: multi-attempt recovery loop / segment-resume — dead code if the wedge isn't software-clearable; revisit only if a recoverable wedge is observed.

- **D5 — Ticker mandatory ≥1 Hz; VU meter deferred.**
  Rationale: Sam ranked the elapsed-time ticker as the must-have "still-recording" proof; the VU meter is low priority. With SD off the UI thread the ticker is free. Keeping LCD bus-grabs minimal (ticker only) reduces mutex contention with the writer.
  Rejected: restore VU now — more frequent LCD redraws = more bus contention, against stability-first.

- **D6 — Never auto-format. Hard requirement.**
  Rationale: `format_if_empty=true` reformatted and wiped the user's recordings during recovery on 2026-05-31. On mount failure, show an error and refuse to record; preserve the card untouched.
  Rejected: keep auto-format for convenience — directly caused data loss.

- **D7 — SPI clock stays 25 MHz.**
  Rationale: the card wrote clean at all speeds in isolation, so clock is not causal. A higher clock shortens the per-block bus-hold, *reducing* mutex contention.
  Rejected: conservative clock margin — sacrifices throughput for no reliability gain.

- **D8 — One app-level SPI-bus mutex; the LCD yields.**
  Rationale: the bus has exactly two clients — LCD flush (core 1) and SD writer (core 0); camera and touch are I2C, no contention. The LCD uses try-take with a short timeout and **skips the frame on miss** (the ticker catches up next second); the writer holds the mutex for the duration of each block write. Capture never touches the mutex.
  Rejected: priority-inheriting blocking take on the LCD side — would stall the UI thread (and therefore capture) behind a 1.6 s SD stall, reintroducing the defect.

- **D9 — Capture stays on core 1; SD-writer task pinned to core 0.**
  Rationale: the Arduino `loop()` (capture + UI + LCD flush) runs on core 1. WiFi is OFF during capture, so core 0 is idle and free for the writer. Capture pushes raw PCM into the ring and never blocks on SD or the bus.
  Rejected: writer on core 1 — competes with capture/UI for the same core.

- **D10 — File ownership: only the SD-writer task touches the file.**
  Rationale: capture pushes raw PCM into the ring; the writer owns open, append, the 5 s power-loss header rewrite, and stop-finalise. Single-owner eliminates cross-thread file races.
  Rejected: shared file access with locking — more lock surface, more failure modes.

- **D11 — Stop is a producer→consumer handshake.**
  Rationale: on stop, capture flags "done"; the writer drains the remaining ring bytes, patches the final WAV header, and closes. Guarantees every buffered sample reaches the card before close.
  Rejected: stop closes the file directly from the UI thread — races the writer mid-block.

---

## 4. Functional Requirements

### Ring Buffer & Capture

- **FR1:** A ring buffer of 1 MB is allocated in PSRAM (`MALLOC_CAP_SPIRAM`) at recording start and freed on stop.
- **FR2:** The capture path reads PCM from the mic (`M5.Mic.record`) and pushes raw samples into the ring buffer. The capture path must never perform an SD write and must never block on the SPI-bus mutex.
- **FR3:** If allocation of the ring buffer fails at start, recording does not begin and the user is shown an error. No recording is attempted without the buffer.

### SD-Writer Task

- **FR4:** A dedicated FreeRTOS task, pinned to core 0, drains the ring buffer and writes to the SD card.
- **FR5:** The writer drains in blocks of 32 KB (writes when at least one full block is available; on stop it drains the remainder regardless of size).
- **FR6:** The writer is the sole owner of the WAV file handle. It performs file open, PCM append, the periodic header rewrite, and the stop-finalise. No other thread touches the file.
- **FR7:** Each SD operation by the writer (block write, header seek/rewrite, finalise) is performed while holding the SPI-bus mutex, released immediately after.

### SPI-Bus Mutex

- **FR8:** A single application-level mutex serialises all SPI-bus access between the LCD flush (core 1) and the SD writer (core 0).
- **FR9:** The LCD flush acquires the mutex with a short, bounded timeout (try-take). On failure to acquire, it **skips that frame** and does not block the UI thread.
- **FR10:** The SD-writer task holds the mutex only for the duration of a single SD operation, never across a drain cycle.

### WAV Header Lifecycle

- **FR11:** On start, the writer writes a 44-byte placeholder WAV header (zeroed sizes).
- **FR12:** Every 5 s (`REC_FLUSH_MS`), the writer rewrites the header with current sizes (`seek(0)` → write → `seek(end)`) and flushes, for power-loss safety — losing at most 5 s on an uncontrolled power loss.
- **FR13:** On stop, the writer drains the remaining ring bytes, writes the final header sizes, and closes the file (the §3 D11 handshake).

### Overrun & Recovery

- **FR14:** If the ring buffer fills (the writer cannot keep up because the card has stalled longer than the buffer depth), recording stops cleanly: the writer finalises the WAV to the last successfully written byte, the file is closed, and a visible error is shown. No audio already captured is lost or corrupted.
- **FR15:** On a short write (write returns fewer bytes than requested), the writer makes a single best-effort `SD.end()/begin()` re-init attempt and retries the write once. If the write still short-writes, recording stops per FR14 and the user is shown "SD fault — restart device."
- **FR16:** All stop-with-error paths (FR14, FR15) surface a visible on-screen error state so the user knows recording has stopped — never a silent stop.

### Auto-Format Removal

- **FR17:** SD mount uses `format_if_empty=false`. The device never reformats the card automatically.
- **FR18:** On SD mount failure, the device shows an error, refuses to start recording, and leaves the card untouched.

### Ticker

- **FR19:** During recording, the elapsed-time ticker updates at ≥1 Hz, driven from audio actually written (so it never climbs while no bytes are persisting).

---

## 5. Non-Functional Requirements

- **NFR1:** A single SD-card GC stall of up to ~1.6 s must cause zero sample loss and zero visible UI disruption beyond a momentarily skipped LCD frame.
- **NFR2:** The filesystem must be left consistent on every stop path (normal stop, overrun stop, fault stop, power-off-during-record) — no partial writes or corrupted FAT entries.
- **NFR3:** Capture must not be starved: the mic I2S DMA must be drained into the ring fast enough that no samples are dropped under normal operation (LCD frame skips are acceptable; sample drops are not).
- **NFR4:** PSRAM usage for the ring buffer must not exceed 1 MB and must be freed on stop, leaving headroom for LVGL buffers and future features.

---

## 6. Architecture

```
   core 1 (Arduino loop)                         core 0 (idle, WiFi off)
   ┌───────────────────────────┐                 ┌──────────────────────────┐
   │  Mic (ES7210 → I2S DMA)   │                 │   SD-writer task          │
   │        │                  │                 │                          │
   │        ▼                  │   1 MB PSRAM    │   drain ≥32 KB block      │
   │   capture: M5.Mic.record  │── ring buffer ──▶   take(bus mutex)         │
   │        push PCM ──────────┼──▶ [≈30 s] ─────│   wav_file.write()        │
   │                           │                 │   give(bus mutex)         │
   │   LVGL UI + ticker ≥1 Hz  │                 │   every 5 s: header rewrite│
   │   LCD flush:              │                 │   on stop: drain+finalise │
   │     try-take(bus mutex)   │◀── bus mutex ──▶│                          │
   │     skip frame on miss    │   (LCD ↔ SD)    │                          │
   └───────────────────────────┘                 └──────────────────────────┘
              ▲                                              │
              └──────────── shared SPI bus (LCD + SD) ───────┘
                         camera + touch are I2C (no contention)
```

Data flow: mic → I2S DMA → capture pushes PCM into the PSRAM ring (core 1, never blocks) → SD-writer task drains the ring in 32 KB blocks under the bus mutex (core 0) → SD card. The LCD shares the bus via the same mutex and yields (skips frames) rather than blocking.

---

## 7. Kill List (mandatory removals)

A closed-world remediation: these existing mechanisms are **removed**, not left alongside the fix. Leaving them in produces a contradictory write path.

- **K1:** `g_lcd_flush_suppress` global and the LCD-flush-suppression logic (`AppRecorderModel.cpp:10,121,133`; `m5gfx_lvgl.*`) — replaced by the SPI-bus mutex (FR8–FR10). The LCD updates normally during recording, gated only by the mutex.
- **K2:** The synchronous `wav_file.write()` in `WriteChunk()` (`AppRecorderModel.cpp:192`) — replaced by push-to-ring (FR2). No SD write occurs on the UI thread.
- **K3:** The 12-retry short-write loop (`AppRecorderModel.cpp:193–213`) — obsolete; the ring absorbs stalls and FR15 defines the single re-init attempt.
- **K4:** The SD-short-write → `RolloverFile()` trigger (`AppRecorderModel.cpp:207`) — replaced by stop-with-error (FR14/FR15). The mic-fail rollover trigger is retained (separate defect, §2.2).
- **K5:** `format_if_empty=true` (`AppRecorderModel.cpp:59`) — set to `false` (FR17/FR18).
- **K6:** The temporary `[INSTR]` heap-heartbeat diagnostics (`InstrHeap`, the 15 s heartbeat, the MIC-FIRST-FAIL probe) — remove once the soak passes; they were for chasing this bug.

---

## 8. User-Perspective Walkthrough

Walked through as a real user would experience it, including failure states.

- **Normal meeting:** Sam hits Record, puts the device down. The elapsed-time ticker climbs at ≥1 Hz — his proof it's still recording. The card GCs several times during the hour; each stall is absorbed by the ring; he sees, at most, the ticker hold for a fraction of a second and a skipped LCD frame. He hits Stop; the file is complete and playable.
- **Card genuinely failing:** mid-meeting the card wedges. One re-init attempt fails. Recording stops, the WAV is finalised to the last good second, and the screen shows "SD fault — restart device." Sam loses nothing recorded up to the fault and knows immediately that recording stopped.
- **Card removed / unmountable at start:** Sam hits Record; mount fails; the screen shows an error and refuses to record. The card is **not** reformatted — any existing recordings are safe.
- **Power off during recording:** the existing power-off path finalises via the writer handshake (FR13) before shutdown — no corrupt file. (Critical-battery and power-off flows from the standalone-experience PRD must call the new stop handshake, not the old direct close.)

---

## 9. Verification & Acceptance Criteria

All tests serial-instrumented on hardware. Unit/compile checks alone do not constitute verification.

- **SC1:** Three consecutive 30-minute continuous recordings complete with zero short-writes, zero wedges, and zero dropped samples (verified from serial instrumentation and playable output files).
- **SC2:** The recorder survives a forced/observed card GC stall (≥1 s) with no data loss and no wedge.
- **SC3:** The live elapsed-time ticker updates at ≥1 Hz throughout each recording.
- **SC4:** On a simulated sustained SD failure, recording stops with a finalised, playable file and a visible "SD fault" error — never a silent stop, never a corrupted file.
- **SC5:** On SD mount failure, the device shows an error and does **not** reformat the card (verified: pre-existing files survive).
- **SC6:** No regression on the standalone flows: boot→record→stop→save, WiFi file pull, Headspace notify, power-off-during-record finalisation.
- **SC7:** PSRAM ring buffer is allocated at start and freed at stop (no leak across repeated record/stop cycles — verified via free-PSRAM heartbeat returning to baseline).
- **SC8:** Kill List items K1–K6 are removed from the codebase (grep-verified; the build contains no `g_lcd_flush_suppress`, no UI-thread `wav_file.write`, `format_if_empty=false`).

---

## 10. Technical Context (for implementer)

Guidance, not requirements — grounded in the current codebase.

- **Audio format:** `REC_SAMPLE_RATE 16000`, 16-bit, mono; `REC_CHUNK_SIZE 1024` samples = 2048 B/chunk = 32 KB/s. Keep this format (Parakeet-native, no resampling).
- **PSRAM:** 8 MB total, ~8.2 MB free during recording; current consumer is the LVGL double draw-buffer (~102 KB). Allocate the ring via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`.
- **Model:** `AppRecorderModel` is already an app-level singleton (`g_app_recorder_model`, `AppRecorderModel.cpp:30`) — no promotion needed. `WriteChunk()` is the function to re-architect.
- **WAV header:** `RecWavHeader` is 44 bytes; `WriteHeader()` (`AppRecorderModel.cpp:92`) already does the seek(0)/write/seek(end) pattern — move its callers into the writer task. `REC_FLUSH_MS` is the 5 s rewrite cadence.
- **SD mount:** `SD.begin(GPIO_NUM_4, SPI, 25000000, "/sd", 5, /*format_if_empty=*/false)` — the only change to the begin call is the final arg (FR17).
- **Bus hazard:** the LCD reuses MISO as D/C (`AppRecorderModel.cpp:8`) — this is exactly why the mutex must serialise LCD and SD; never let an LCD access interleave a live SD transaction.
- **Threading:** Arduino `loop()` runs on core 1 (`ARDUINO_RUNNING_CORE`); pin the writer with `xTaskCreatePinnedToCore(..., /*core=*/0)`. WiFi is off during capture, so core 0 is free.
- **Mic-fail rollover:** `RolloverFile()` and the mic-fail-streak logic stay; only the SD-short-write call site (`AppRecorderModel.cpp:207`) is removed (K4).
- **Power-off/critical-battery flows:** ensure the standalone-experience finalise paths (FR29/FR31/FR32 of `recorder-standalone-experience-prd.md`) route through the new writer stop handshake (FR13), not a direct `wav_file.close()`.

---

## 11. Open Items for Implementation Detail (Chip)

- Confirm whether a single `SD.end()/begin()` actually clears the observed wedge; if it cannot, drop FR15's re-init to stop-only (the re-init becomes dead code).
- Choose the ring primitive (FreeRTOS stream buffer vs. custom PSRAM ring + task notification) and the capture-side push mechanism (LVGL timer vs. dedicated capture task) such that capture never blocks on the bus mutex.
- Confirm the exact `MALLOC_CAP_SPIRAM` free figure on the next boot to validate the 1 MB allocation headroom.
