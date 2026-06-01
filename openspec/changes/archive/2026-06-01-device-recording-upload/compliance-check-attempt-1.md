# Compliance Check — device-recording-upload (Attempt 1)

**Date:** 2026-06-01 20:15:49 AEST
**PRD:** docs/prds/recorder/device-recording-upload-prd.md
**Verdict:** COMPLIANT
**Compilation:** PASS (`pio run -e m5stack-cores3` exit 0, Flash 95.4%, RAM 33.3%)

## Requirements Scorecard

| Status | Count | % |
|--------|-------|---|
| PASS | 16 | 100% |
| PARTIAL | 0 | 0% |
| FAIL | 0 | 0% |
| VIOLATED | 0 | 0% |
| **Total** | **16** | |

Inventory covers FR1–FR14 plus §5 upload contract and §7 superseded-mechanism requirements. Acceptance criteria SC1–SC8 are verification procedures: SC7 (notify path removed) is statically verifiable and PASSES (grep clean); SC1–SC6, SC8 require the physical device + companion Headspace endpoint and are out of this PRD's bench scope (§12) — tasks 3.3–4.3 intentionally unchecked.

## Requirement Trace

| ID | Requirement | Status | Location |
|----|-------------|--------|----------|
| FR1 | mDNS browse `_otl-recordings._tcp`, build upload URL | PASS | RecorderServer.cpp:247-256 |
| FR2 | Fallback `HEADSPACE_UPLOAD_URL`; disable+log if both empty | PASS | RecorderServer.cpp:257-262 |
| FR3 | Re-browse on each WiFi reconnect | PASS | RecorderServer.cpp:81,102-107 |
| FR4 | Iterate REC_*.wav, skip acked | PASS | RecorderServer.cpp:298-311 |
| FR5 | Raw audio/wav body + X-Device-Id/X-Filename/X-File-Size | PASS | RecorderServer.cpp:330-333 |
| FR6 | Sequential, oldest first, no concurrency | PASS | RecorderServer.cpp:290-358 (one file/call) |
| FR7 | Idle + connected gate | PASS | RecorderServer.cpp:72,120 |
| FR8 | 201 + bytes_written match → ackFile | PASS | RecorderServer.cpp:344-350 |
| FR9 | Non-201 / mismatch → no ack, retry | PASS | RecorderServer.cpp:351-357 |
| FR10 | No pre-check before POST (idempotent) | PASS | RecorderServer.cpp:290-338 (no HEAD/GET) |
| FR11 | UPLOAD_RETRY_MS 30 s backoff | PASS | RecorderServer.cpp:38,121 |
| FR12 | Return to idle; re-run on tick/stop/boot | PASS | RecorderServer.cpp:111,121; AppRecorder.cpp:110,217 |
| FR13 | WiFi OFF during recording, re-enable on stop | PASS | RecorderServer.cpp:80,87-90 |
| FR14 | Upload replaces notifyHeadspace | PASS | grep clean; RecorderServer.cpp:120-123 |
| §5 | Upload contract (path, headers, 201 semantics) | PASS | RecorderServer.cpp:325-346 |
| §7 | Notify removed, NOTIFY_URL→UPLOAD_URL, pull-ack removed, diag endpoints retained | PASS | grep clean; RecorderServer.cpp:140-141,207-209 |

## Failures and Violations

None.

## Partial Implementations

None.

## Scope Creep

| File | Change | Classification |
|------|--------|----------------|
| src/net/RecorderServer.cpp | Replace notify cycle with upload cycle; mDNS browse; SPI mutex on SD reads; ack on confirmed 201 | Traces to PRD (FR1–FR14, §5, §7) |
| src/net/RecorderServer.h | Remove notify members; add upload-cycle members; `requestUpload()` | Traces to PRD (FR14, §7) |
| src/pages/AppRecorder/AppRecorder.cpp | `requestNotify()` → `requestUpload()` at both recording-stop sites | Traces to PRD (FR14) |
| include/secrets.h.example | `HEADSPACE_NOTIFY_URL` → `HEADSPACE_UPLOAD_URL` | Supporting infrastructure (D7, §11) |
| openspec/changes/device-recording-upload/* | Proposal, tasks, spec | OpenSpec artifacts |

No invented features. No scope creep.

## Compilation Status

Clean build. `pio run -e m5stack-cores3` exit 0 — Flash 95.4% (7005025/7340032), RAM 33.3%. The `g_spi_bus_mutex` dependency from the SD Write Architecture Remediation PRD is present (`lib/m5gfx_lvgl/m5gfx_lvgl.cpp:9,100`). The bare `pio run` native Unity host env failure is pre-existing and unrelated (native env does not compile src/; errors confined to src/pages/AppFiles/* which this change does not touch).

## OpenSpec Alignment

- **proposal.md:** aligned — all PRD requirements addressed; BREAKING removal of notify documented; SPI-mutex dependency noted as landed.
- **tasks.md:** 17/17 implementation tasks (2.1–2.17) complete and traced to FR/D references with "Verify by:" annotations satisfied. Tasks 3.3–4.3 intentionally unchecked (device/companion-endpoint bench SC verification, out of scope per §12). No orphan tasks.
- **spec.md:** accurate — six ADDED requirements (mDNS discovery, fallback URL, device-push upload, confirmed-disk ack, idle-only/backoff, notify replacement) match PRD intent and scenarios.
