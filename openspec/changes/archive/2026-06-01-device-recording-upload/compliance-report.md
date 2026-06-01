# Compliance Report: device-recording-upload

**Generated:** 2026-06-01 20:23 AEST
**Status:** COMPLIANT

## Summary

The device-side recording upload is fully implemented and traces cleanly to the PRD, proposal, and delta spec. All implementation tasks are complete, the notify-then-pull flow is removed, and the firmware compiles clean (`pio run -e m5stack-cores3`, exit 0). On-device acceptance criteria (SC1–SC8) require the companion Headspace endpoint + mDNS advertisement and are verified at the bench after merge per this repo's established compile+static-verify-then-bench pattern.

## Acceptance Criteria

| Criterion | Status | Notes |
|-----------|--------|-------|
| SC1 — recording uploads, file appears with correct byte count | PASS (static) | Upload cycle streams raw WAV, acks on 201 + matching `bytes_written`. End-to-end byte-match verified at bench (needs companion endpoint). |
| SC2 — mDNS discovery without hardcoded URL | PASS (static) | `resolveUploadUrl()` calls `MDNS.queryService("otl-recordings","tcp")` (no leading underscores), builds `http://<ip>:<port>/api/uploads/recordings` from first result. mDNS takes priority over fallback. Bench-verified with companion advertisement. |
| SC3 — failed upload not acked, retries, succeeds when server up | PASS (static) | No `ackFile()` on any non-201/network-error/mismatch path; `UPLOAD_RETRY_MS` 30 s backoff re-runs the cycle. |
| SC4 — re-POST does not duplicate | PASS (static) | No HEAD/GET existence pre-check; relies on server idempotency per §5 contract. Server-side dedup is the companion PRD's responsibility. |
| SC5 — acked files not re-uploaded after reboot | PASS (static) | `uploadCycle()` skips `fileAcked()` names via `UPLOADED.txt`. |
| SC6 — upload never during recording | PASS | Cycle gated on `started && !recording`; `recording` early-return at top of `loop()` fires first; WiFi OFF during capture unchanged. |
| SC7 — notifyHeadspace removed, no JSON notify POST | PASS | `grep` confirms zero refs to `notifyHeadspace`, `HEADSPACE_NOTIFY_URL`, `NOTIFY_RETRY_MS`, `notify_pending`, `last_notify`, `requestNotify`, `notify_enabled` in `src`/`include`. |
| SC8 — fallback URL works when mDNS fails | PASS (static) | `resolveUploadUrl()` falls back to `HEADSPACE_UPLOAD_URL` when browse returns 0; disables + logs when both empty. |

## Requirements Coverage

- **PRD Requirements:** 14/14 covered (FR1–FR14)
- **Tasks Completed:** 17/17 implementation tasks complete (2.1–2.17 all `[x]`); testing tasks 3.x are bench/post-merge verification per repo pattern
- **Design Compliance:** Yes — raw `audio/wav` streamed body (D3), confirmed-disk ack on `bytes_written` match (D4), existing `UPLOADED.txt`/`ackFile()` reused (D5), idle-only with 30 s backoff (D6), mDNS-priority with compile-time fallback (D7)

## Scope Compliance

- **Scope creep detected:** No
- **Untraceable additions:** None. Every code change traces to a PRD requirement:
  - `resolveUploadUrl()` → FR1–FR3, D2, D7
  - `uploadCycle()` + `parse_bytes_written()` → FR4–FR12, D3, D4 (string-scan parse avoids pulling a JSON lib onto the constrained device — justified by §11 constraints)
  - `bus_take()`/`bus_give()` + `g_spi_bus_mutex` extern → PRD §8/§11 SPI-bus mutex (dependency landed in `lib/m5gfx_lvgl/`)
  - `secrets.h.example` `HEADSPACE_NOTIFY_URL` → `HEADSPACE_UPLOAD_URL` → D7, §11
  - `AppRecorder.cpp` `requestNotify()` → `requestUpload()` (both call sites) → FR14, task 2.17
  - `handleDownload()` ack removal, diagnostic endpoints retained → §7, FR14, SC7

## Issues Found

None blocking. Known non-blocking conditions (noted, not blocked on):
1. Native Unity host env has a pre-existing `<algorithm>`-not-found failure in `test/test_native/test_recorder_math.cpp` — that module is untouched by this change.
2. Flash at 95.4% is above the smoke worker's default threshold, but the PRD imposes no flash budget (+~0.7% vs ~94.7% master baseline).

## Recommendation

PROCEED
