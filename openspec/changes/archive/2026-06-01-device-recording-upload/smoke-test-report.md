# Smoke Test Report — device-recording-upload

**Date:** 2026-06-01 20:21 AEST
**Compilation:** clean (firmware gate `pio run -e m5stack-cores3` — exit 0, no warnings)
**Checks executed:** 8

## Resource Budget

| Resource | Used | Available | % | Status |
|----------|------|-----------|---|--------|
| Flash | 7,005,025 bytes | 7,340,032 bytes | 95.4% | WARN (addendum — see below) |
| RAM | 109,204 bytes | 327,680 bytes | 33.3% | OK |

## Results

| # | Check | Target | Result | Severity | Evidence |
|---|-------|--------|--------|----------|----------|
| 1 | Firmware compile | `pio run -e m5stack-cores3` | PASS | — | Exit 0, SUCCESS, 0 warnings on changed files |
| 2 | Flash usage | app partition | WARN | MINOR (addendum) | 95.4% (7005025/7340032); +~0.7% vs master ~94.7%; no PRD flash budget |
| 3 | RAM usage | SRAM | PASS | — | 33.3% of 327680 bytes |
| 4 | Include chain | changed files | PASS | — | RecorderServer.h included by .cpp + AppRecorder.cpp; ESPmDNS/HTTPClient/SD declared in platformio deps; `g_spi_bus_mutex` extern matches def in m5gfx_lvgl.cpp |
| 5 | Stale notify symbols removed | src/ include/ | PASS | — | grep for notifyHeadspace / HEADSPACE_NOTIFY_URL / notify_pending / last_notify / requestNotify / NOTIFY_RETRY_MS → 0 hits (SC7) |
| 6 | Call-site wiring | AppRecorder.cpp | PASS | — | requestNotify() replaced by requestUpload() at both former notify sites (L110, L217); setRecording() intact |
| 7 | Config consistency | secrets.h.example | PASS | — | HEADSPACE_UPLOAD_URL declared; HEADSPACE_NOTIFY_URL absent; `#ifndef HEADSPACE_UPLOAD_URL` fallback present in RecorderServer.cpp |
| 8 | Upload-path integrity | RecorderServer.cpp | PASS | — | Streamed `Stream*` POST (no RAM buffer); SPI mutex held across transfer; ack only on 201 + bytes_written==fileSize; idle-only gate (`started && !recording`) |

## Reconciliation Addendum (non-blocking, verified by lead)

1. **Flash 95.4%** — The PRD imposes no flash budget. Master baseline ~94.7%; this change adds ~0.7% of PRD-traceable upload code. Per recorder-standalone PR #2 precedent, the smoke flash threshold is a worker default, not a PRD constraint. Trimming working code to chase it would be reverse scope-creep. Documented, not failed.
2. **`native` Unity host env pre-existing failure** — `<algorithm> not found` in `test/test_native/test_recorder_math.cpp`, a module this change never touched (not in the diff). Not attributable to this change. The firmware compile gate is `pio run -e m5stack-cores3` and it passes clean.

## Failures

None introduced by this change. No CRITICAL or MAJOR findings.

## Verdict

ALL_PASSED — 0 critical, 0 major, 1 minor (flash usage, documented addendum / non-blocking)
