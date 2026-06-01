---
validation:
  status: valid
  validated_at: '2026-06-01T20:00:52+10:00'
---

## Product Requirements Document (PRD) — Device Recording Upload

**Project:** CoreS3 Meeting Recorder
**Scope:** Device pushes recorded WAV files to the Headspace server over home Wi-Fi, discovers the server via mDNS, and tracks which files have been sent.
**Author:** Robbo (workshopped with Sam, Chip, and Mark, #workshop-recorder-transcription-315)
**Date:** 2026-06-01
**Status:** Draft
**Type:** New feature

---

## Executive Summary

The CoreS3 recorder captures meeting audio to SD card. Today, Headspace pulls recordings from the device after receiving a JSON notification. This PRD inverts the flow: the device discovers Headspace on the local network via mDNS service browse and pushes WAV files directly to a Headspace REST endpoint. The device acks a file only when the server confirms it has been persisted to disk — no silent data loss.

This is the device-side half of a two-PRD unit. A companion Headspace PRD (workshopped separately) covers the receiving endpoint, Deepgram transcription with speaker diarization, and transcript storage. The shared interface between the two PRDs is the upload contract defined in §5.

---

## 1. Context & Purpose

### 1.1 Current State

The device records 16 kHz mono WAV to SD, serves files over HTTP (`GET /rec/<name>`), and POSTs a JSON notification to Headspace on recording stop and boot. Headspace then pulls the files. This notify-and-pull flow was verified end-to-end on 2026-05-28.

### 1.2 Why Change

The pull model requires Headspace to initiate a transfer back to the device — an extra round-trip and an inversion of responsibility (the server reaches back to the embedded device). A device-push model is simpler: the device owns the entire upload lifecycle, and the server is a passive receiver. It also eliminates the single-threaded deadlock risk in the current notify flow (the device can't serve the pull while blocked waiting for the notify response).

### 1.3 Why Now

The recorder hardware is stable (SD write architecture remediated, battery cold-start fixed). The next pipeline stage — automated meeting transcription — requires recordings to arrive at Headspace reliably. This is the first leg of that pipeline.

---

## 2. Scope

### 2.1 In Scope

- mDNS service browse to discover the Headspace upload endpoint on the LAN.
- Compile-time fallback URL in `secrets.h` for bench testing when mDNS is unavailable.
- HTTP POST of raw WAV file body to the discovered endpoint.
- Sent-tracking on SD card — reuse of the existing `UPLOADED.txt` ack marker, with the ack trigger changed from "served on pull" to "server confirmed 201 with bytes persisted."
- Idle-only upload: never during recording, never when WiFi is off.
- Retry with backoff on upload failure.
- Sequential upload of all un-acked files after recording stops and on boot.

### 2.2 Out of Scope / Deferred

- **Headspace receiving endpoint** — covered by the companion Headspace PRD.
- **Transcription** — the device does not transcribe; it ships audio. Transcription is server-side (Headspace PRD).
- **Cloud upload** — this PRD targets LAN upload to Sam's local machine only.
- **mDNS service advertisement by Headspace** — that's a Headspace-side deliverable specified in the companion PRD. This PRD discovers the service; it does not create it.
- **Multi-device support** — the upload contract supports a device identifier header, but coordination between multiple devices is deferred.
- **Compression or format conversion** — audio stays 16 kHz / 16-bit / mono PCM WAV.
- **Deletion of uploaded files from SD** — files remain on the card after upload. Manual cleanup is the user's responsibility for now.

---

## 3. Key Decisions

- **D1 — Device pushes to Headspace (not notify-then-pull).**
  Rationale: eliminates the round-trip and the single-threaded deadlock risk in the current notify flow. The device owns the entire upload lifecycle. The pull-serve endpoints (`GET /api/recordings`, `GET /rec/<name>`) are retained for diagnostics but are no longer the primary transfer path.
  Rejected: keep notify-then-pull — works but has the deadlock risk and requires the server to reach back to the device.

- **D2 — mDNS service discovery (`_otl-recordings._tcp`), not hardcoded IP.**
  Rationale: Sam's machine gets a DHCP-assigned IP on `192.168.4.x` that can change. The device already has the mDNS library (uses it to advertise `core-s3.local`). Headspace advertises `_otl-recordings._tcp` on port 15055 (companion PRD); the device browses for it and gets host + IP + port dynamically. No hardcoded addresses, survives reboots and DHCP changes.
  Rejected: hardcoded IP in `secrets.h` — fragile, breaks on DHCP change. Hardcoded `.local` hostname — finds the host but not the port (Headspace binds to 15055, not a standard port), only half-solves discovery.

- **D3 — Raw `audio/wav` body, not multipart.**
  Rationale: the ESP32 streams the file straight off SD card via `HTTPClient` with a `Stream*` parameter. Raw streaming is simple and reliable for files up to 115 MB (60-min meeting). Multipart encoding on the ESP32 would require building the MIME envelope in constrained RAM — unnecessary complexity for a single-file upload with metadata in headers.
  Rejected: `multipart/form-data` — more complex on the device side for no benefit. The server adapts to accept raw body.

- **D4 — Ack only on confirmed disk-write 201, not bytes-received.**
  Rationale: the device must never mark a file as sent unless the server has confirmed the bytes are persisted to disk. A 200/201 that fires before the disk write completes is a silent data loss bug. The server's 201 response includes the byte count actually written; the device verifies it matches before acking.
  Rejected: ack on any 2xx — "received" is not "persisted."

- **D5 — Reuse existing `UPLOADED.txt` ack marker.**
  Rationale: the sent-tracking file and `fileAcked()`/`ackFile()` functions already exist and work. The only change is the ack trigger: from "served on pull" (line 198 of `RecorderServer.cpp`) to "server confirmed 201 with matching byte count."
  Rejected: new tracking mechanism — unnecessary when the existing one works.

- **D6 — Idle-only upload with existing 30 s backoff loop.**
  Rationale: WiFi is OFF during recording (existing behaviour, prevents I2S DMA starvation). Uploads run only when the device is idle. The existing `NOTIFY_RETRY_MS` (30 s) backoff loop is reused for upload retry cadence.
  Rejected: upload during recording — directly conflicts with the SD write architecture (shared SPI bus, I2S capture priority).

- **D7 — Compile-time fallback URL for bench testing.**
  Rationale: mDNS browse may not resolve in all environments (bench without Headspace running, networks that block mDNS). A `HEADSPACE_UPLOAD_URL` in `secrets.h` provides a direct fallback. If mDNS resolves, it takes priority; if not, the fallback URL is used.
  Rejected: mDNS only — too brittle for development.

---

## 4. Functional Requirements

### Service Discovery

- **FR1:** On WiFi connect, the device browses for mDNS service `_otl-recordings._tcp`. On success, it extracts the host IP and port from the service record and constructs the upload URL.
- **FR2:** If mDNS browse fails or times out, the device falls back to `HEADSPACE_UPLOAD_URL` from `secrets.h`. If both are empty/unavailable, uploads are disabled and a serial log message is emitted.
- **FR3:** mDNS browse is re-attempted on each WiFi reconnect (DHCP lease may have changed the server's IP).

### File Upload

- **FR4:** The device iterates all `REC_*.wav` files on the SD card, skipping any already in `UPLOADED.txt`. For each un-acked file, it performs an HTTP POST to the upload endpoint.
- **FR5:** The POST sends the raw WAV file body streamed from SD. Content-Type is `audio/wav`. Metadata is sent in headers: `X-Device-Id` (device hostname, e.g. `core-s3`), `X-Filename` (original filename, e.g. `REC_001.wav`), `X-File-Size` (file size in bytes).
- **FR6:** Files are uploaded sequentially, oldest first. No concurrent uploads.
- **FR7:** Upload only runs when idle (not recording) and WiFi is connected. The existing `recording` flag and WiFi state checks gate upload attempts.

### Ack & Integrity

- **FR8:** On a `201 Created` response, the device reads the `bytes_written` field from the JSON response body. If it matches the file size sent, the file is acked via `ackFile()`. If it does not match, the file is not acked and will be retried.
- **FR9:** On any non-201 response, the device does not ack. The file will be retried on the next upload cycle.
- **FR10:** Re-POST of the same file (same device-id + filename) is safe — the server endpoint is idempotent (overwrites/dedupes, never duplicates). The device does not need to check whether the server already has the file.

### Retry & Backoff

- **FR11:** After a failed upload (non-201 or network error), the device waits `UPLOAD_RETRY_MS` (30 s, reusing the existing backoff constant) before the next attempt.
- **FR12:** After all un-acked files are uploaded (or all attempts fail), the device returns to its normal idle loop. The upload cycle re-runs on the next backoff tick, on recording stop, and on boot.

### WiFi Lifecycle

- **FR13:** WiFi remains OFF during recording (existing behaviour, `WiFi.mode(WIFI_OFF)` in `RecorderServer::loop()`). On recording stop, WiFi re-enables and uploads begin after reconnect.
- **FR14:** The upload cycle is the replacement for `notifyHeadspace()`. The notify POST is removed; the push upload replaces it.

---

## 5. Upload Contract (Shared with Headspace PRD)

This contract is the interface between the two PRDs. Both sides build to this specification.

| Field | Value |
|-------|-------|
| Method | `POST` |
| Path | `/api/uploads/recordings` |
| Body | Raw WAV bytes (streamed) |
| Content-Type | `audio/wav` |
| `X-Device-Id` | Device hostname (e.g. `core-s3`) |
| `X-Filename` | Original filename (e.g. `REC_001.wav`) |
| `X-File-Size` | File size in bytes |
| Audio format | 16 kHz mono 16-bit PCM WAV |
| Typical sizes | ~1.9 MB/min; 30-min ~ 57 MB; 60-min ~ 115 MB |
| Success | `201 Created`, body: `{"filename": "...", "bytes_written": N}` |
| Meaning of 201 | File persisted to disk, complete. Not "bytes received." |
| Failure | `4xx`/`5xx` — device does not ack, retries next cycle |
| Idempotency | Re-POST of same device-id + filename overwrites, never duplicates |
| Uniqueness | Key on device-id + filename. Files land under `uploads/recordings/<device-id>/` |
| Discovery | Device browses for `_otl-recordings._tcp` via mDNS |

---

## 6. Architecture

```
  Device (CoreS3)                              Sam's machine (Headspace)
  ┌─────────────────────────┐                  ┌──────────────────────────┐
  │  SD card: REC_*.wav     │                  │  mDNS: _otl-recordings   │
  │         │               │    WiFi LAN      │        ._tcp             │
  │  on idle:               │                  │         │                │
  │    mDNS browse ─────────┼──── discover ───▶│    host + port           │
  │    for each un-acked:   │                  │         │                │
  │      POST /api/uploads/ │                  │         ▼                │
  │        recordings       │── raw WAV body ─▶│  REST endpoint           │
  │      verify 201 + bytes │◀── 201 + json ───│  write to disk           │
  │      ackFile() on match │                  │  uploads/recordings/     │
  │         │               │                  │    core-s3/REC_NNN.wav   │
  │  UPLOADED.txt (SD)      │                  │         │                │
  │  tracks sent files      │                  │    (PRD 2: Deepgram      │
  │                         │                  │     transcription)       │
  └─────────────────────────┘                  └──────────────────────────┘
```

Data flow: SD card → idle-only upload loop → mDNS discovers Headspace → HTTP POST raw WAV → server writes to disk → 201 with `bytes_written` → device acks → next file.

---

## 7. Superseded Mechanisms

The notify-then-pull flow is superseded by device-push. Specific changes:

- **`notifyHeadspace()`** — removed. The JSON notification POST is replaced by the direct file push. The upload cycle (FR4–FR12) replaces the notify cycle.
- **`HEADSPACE_NOTIFY_URL`** — replaced by `HEADSPACE_UPLOAD_URL` as the compile-time fallback (D7).
- **Pull-triggered ack** (`handleDownload()` line 198) — the ack trigger moves from "file served on pull" to "server confirmed 201 with matching byte count" (FR8).
- **`GET /api/recordings` and `GET /rec/<name>`** — retained for diagnostics and manual access. Not removed.

---

## 8. Known Constraints

- **Single-mic mono audio.** Diarization accuracy (handled server-side in PRD 2) depends on mic placement and room acoustics. Far speakers and crosstalk reduce accuracy. This is inherent to the hardware — not a defect, but set expectations: "best-effort speaker labels from a single far-field mic." (Flagged by Chip, workshop #315.)
- **Large file transfer times.** A 60-min recording (~115 MB) over WiFi will take minutes to upload. The device is single-threaded; during upload the UI is responsive but no recording can start. Acceptable for the "put device down after meeting, it uploads while idle" use case.
- **SD card SPI bus contention.** The upload reads from SD while the LCD may also access the bus. The SPI-bus mutex from the SD write architecture remediation PRD protects this — the upload path must acquire the mutex for SD reads, same as the writer task does for writes.

---

## 9. User-Perspective Walkthrough

- **Normal meeting:** Sam records a meeting, hits stop. WiFi re-enables, the device discovers Headspace via mDNS, and uploads the recording. Sam sees nothing — it just works. The file appears in `uploads/recordings/core-s3/` on his machine.
- **Multiple recordings:** Sam records three meetings before returning to WiFi range. On reconnect, the device uploads all three sequentially, oldest first. Each is acked individually.
- **Network glitch mid-upload:** WiFi drops during a transfer. The file is not acked. On reconnect, the device retries. The server endpoint is idempotent — a partial re-upload overwrites cleanly.
- **Headspace not running:** mDNS browse fails, and no fallback URL is configured. Uploads are disabled; files stay on SD. Serial log indicates the failure. Next boot or WiFi reconnect, the device tries again.
- **Already-uploaded files:** Sam power-cycles the device. On boot, it checks `UPLOADED.txt`, skips files already acked, and uploads only new ones.

---

## 10. Verification & Acceptance Criteria

- **SC1:** A recording completes, the device uploads it to Headspace, and the file appears in `uploads/recordings/core-s3/` with the correct byte count — verified by comparing file sizes on SD and server.
- **SC2:** The device discovers Headspace via mDNS (`_otl-recordings._tcp`) without a hardcoded URL — verified by leaving `HEADSPACE_UPLOAD_URL` empty and confirming upload succeeds.
- **SC3:** On a failed upload (server down, network error), the device does not ack the file, retries on the next cycle, and eventually succeeds when the server is available — verified by starting the device with Headspace stopped, then starting Headspace.
- **SC4:** Re-uploading the same file (re-POST) does not create a duplicate on the server — verified by manually clearing the ack marker and re-uploading.
- **SC5:** Files already in `UPLOADED.txt` are not re-uploaded after a reboot — verified by checking serial logs show "skipped" for acked files.
- **SC6:** Upload never runs during recording — verified by starting a recording and confirming no HTTP activity in serial logs.
- **SC7:** The `notifyHeadspace()` code path is removed and the device no longer sends JSON notification POSTs — verified by grep and serial log.
- **SC8:** Fallback to `HEADSPACE_UPLOAD_URL` works when mDNS browse fails — verified by disabling mDNS on the server and setting the compile-time URL.

---

## 11. Technical Context (for implementer)

- **HTTPClient streaming:** `HTTPClient` supports `POST` with a `Stream*` parameter (the SD `File` object). This streams the file off SD without loading it into RAM. Use `http.sendRequest("POST", &file, fileSize)` or equivalent.
- **Existing plumbing:** `notifyHeadspace()` in `RecorderServer.cpp` already uses `HTTPClient` for outbound POST. The upload replaces the notify body with the file stream and adds the metadata headers.
- **Ack infrastructure:** `fileAcked()` and `ackFile()` with `UPLOADED.txt` are unchanged. The call site moves from `handleDownload()` (pull ack) to the upload response handler (push ack).
- **mDNS browse:** ESP32 `ESPmDNS` library supports service browse via `MDNS.queryService("otl-recordings", "tcp")` — **note: no leading underscores** (the library prepends `_` to both the service and proto internally; passing `"_otl-recordings"`/`"_tcp"` makes the browse silently return 0 results). The call returns a count `n`; read each result with `MDNS.hostname(i)`, `MDNS.IP(i)`, `MDNS.port(i)`. Call after WiFi connect; cache the result until WiFi reconnects. **mDNS is torn down and re-initialised every recording→idle transition** (WiFi goes fully OFF during recording and `started` resets to false at `RecorderServer.cpp:73`), so the browse must re-run after each `MDNS.begin()` re-init, not just at boot.
- **SPI bus mutex:** if the SD write remediation PRD has landed, acquire the SPI-bus mutex before SD reads during upload. If it hasn't landed yet, this is a sequencing dependency — the upload PRD should build after the remediation.
- **secrets.h update:** replace `HEADSPACE_NOTIFY_URL` with `HEADSPACE_UPLOAD_URL`. Same conditional-include pattern.
- **Upload during idle only:** the existing `recording` flag and WiFi-off-during-capture behaviour (lines 64–86 of `RecorderServer.cpp`) already gate network activity. The upload loop slots into the same idle window where `notifyHeadspace()` currently runs.

---

## 12. Sequencing Dependency

This PRD depends on the **SD Write Architecture Remediation PRD** for the SPI-bus mutex. Upload reads from SD while the LCD may also access the shared SPI bus — the mutex must be in place. If the remediation has not been implemented, the upload should build after it.

The companion **Headspace Recording Transcription Pipeline PRD** must implement the receiving endpoint (§5 contract) and the mDNS service advertisement (`_otl-recordings._tcp`) before end-to-end verification is possible.
