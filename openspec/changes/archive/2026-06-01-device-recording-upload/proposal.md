## Why

Today Headspace pulls recordings from the device after a JSON notification (`notifyHeadspace()` → server pulls via `GET /rec/<name>`). This inverts responsibility (server reaches back to the embedded device), adds a round-trip, and carries a single-threaded deadlock risk: the device cannot serve the pull while blocked waiting for the notify response. This change makes the device push WAV files directly to a Headspace REST endpoint discovered via mDNS, so the device owns the entire upload lifecycle and the server is a passive receiver. (PRD §1.2, D1.)

## What Changes

- **mDNS service discovery** — on WiFi connect, browse for `_otl-recordings._tcp`, extract host IP + port, and construct the upload URL. Re-browse on each WiFi reconnect. (FR1, FR3, D2.)
- **Compile-time fallback URL** — `HEADSPACE_UPLOAD_URL` in `secrets.h` used when mDNS fails; mDNS takes priority. If both unavailable, uploads are disabled with a serial log. (FR2, D7.)
- **Device-push upload** — iterate un-acked `REC_*.wav`, POST each as a raw `audio/wav` body streamed off SD, oldest first, sequentially, with metadata headers `X-Device-Id`, `X-Filename`, `X-File-Size`. (FR4, FR5, FR6, D3.)
- **Confirmed-disk ack** — ack a file via the existing `ackFile()`/`UPLOADED.txt` only on `201 Created` when the response `bytes_written` matches the file size sent; otherwise do not ack and retry. (FR8, FR9, FR10, D4, D5.)
- **Idle-only upload with 30 s backoff** — upload runs only when not recording and WiFi connected, reusing the existing `NOTIFY_RETRY_MS`/30 s backoff (renamed conceptually to `UPLOAD_RETRY_MS`); cycle re-runs on backoff tick, recording stop, and boot. (FR7, FR11, FR12, FR13, D6.)
- **SPI-bus mutex on SD reads** — acquire the existing `g_spi_bus_mutex` for SD reads during upload (shared LCD/SD bus). (PRD §8, §11; sequencing dependency satisfied — mutex has landed.)
- **BREAKING** — `notifyHeadspace()` and the `HEADSPACE_NOTIFY_URL` notify POST are **removed**; the push upload cycle replaces them. The pull-triggered ack at `handleDownload()` line 198 is removed (ack moves to the upload response handler). `GET /api/recordings` and `GET /rec/<name>` are **retained** for diagnostics. (FR14, §7, SC7.)

## Impact

- Affected specs: `recorder-upload` (new capability — device-side recording upload)
- Affected code:
  - `src/net/RecorderServer.cpp` — replace `notifyHeadspace()` with the upload cycle; move ack out of `handleDownload()`; add mDNS browse + fallback URL resolution; rename `NOTIFY_RETRY_MS` → `UPLOAD_RETRY_MS`; acquire `g_spi_bus_mutex` for SD reads
  - `src/net/RecorderServer.h` — update method/member declarations (remove `notifyHeadspace`/`notify_pending`/`requestNotify` as wired to notify; add upload-cycle members)
  - `include/secrets.h` — replace `HEADSPACE_NOTIFY_URL` with `HEADSPACE_UPLOAD_URL`
  - Callers of `requestNotify()`/notify plumbing (e.g. `AppRecorder`) updated to the upload trigger
- Shared interface: upload contract `POST /api/uploads/recordings` (PRD §5) — built to by the companion Headspace PRD
- Dependency: SPI-bus mutex from the SD Write Architecture Remediation PRD — verified landed (`g_spi_bus_mutex` in `lib/m5gfx_lvgl/`). Companion Headspace PRD must provide the receiving endpoint + mDNS advertisement before end-to-end verification.
