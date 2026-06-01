## 1. Planning (Phase 1)

- [x] 1.1 Create OpenSpec proposal files
- [x] 1.2 Validate proposal with openspec validate
- [x] 1.3 Review and get approval

## 2. Implementation (Phase 2)

### Service Discovery (FR1–FR3, D2, D7)

- [ ] 2.1 Replace `HEADSPACE_NOTIFY_URL` with `HEADSPACE_UPLOAD_URL` in `secrets.h` and the `#ifndef` fallback block in `RecorderServer.cpp` (same conditional-include pattern). (FR2, D7, §11 secrets.h)
  Verify by: grep shows no `HEADSPACE_NOTIFY_URL` in src/include; `HEADSPACE_UPLOAD_URL` `#ifndef` fallback present in RecorderServer.cpp and defined in secrets.h.example.
- [ ] 2.2 Add an mDNS browse for `_otl-recordings._tcp` using `MDNS.queryService("otl-recordings", "tcp")` (no leading underscores — library prepends them). Read result `n`; for the first record extract host via `MDNS.hostname(i)`, IP via `MDNS.IP(i)`, port via `MDNS.port(i)`; construct the upload URL `http://<ip>:<port>/api/uploads/recordings`. (FR1, §11 mDNS browse)
  Verify by: a `resolveUploadUrl()`/browse method calls `MDNS.queryService("otl-recordings", "tcp")` with no leading underscores and builds `http://<ip>:<port>/api/uploads/recordings` from the first result.
- [ ] 2.3 Run the browse after WiFi connect and cache the resolved URL. Re-run the browse after each `MDNS.begin()` re-init on the recording→idle transition (mDNS is torn down when WiFi goes OFF and `started` resets to false). (FR3, §11 mDNS browse)
  Verify by: browse runs in the `connected && !started` block after `MDNS.begin()`; resolved URL cached in a member and re-resolved each time `started` flips back to true.
- [ ] 2.4 If mDNS browse returns 0 results or times out, fall back to `HEADSPACE_UPLOAD_URL`. If both are empty/unavailable, disable uploads and emit a serial log message. mDNS takes priority over the fallback when both resolve. (FR2)
  Verify by: when browse returns 0, code assigns the compile-time `HEADSPACE_UPLOAD_URL`; when both empty, uploads disabled with a `Serial.println` log.

### File Upload (FR4–FR7, D3)

- [ ] 2.5 Add an upload-cycle method that iterates all `REC_*.wav` files on SD, skipping any already in `UPLOADED.txt` via `fileAcked()`, oldest first, one at a time (no concurrent uploads). (FR4, FR6)
  Verify by: `uploadCycle()` opens `/`, iterates `is_recording_file` entries, skips `fileAcked()` names, processes one file per call.
- [ ] 2.6 For each un-acked file, open it from SD and POST the raw WAV body streamed via `HTTPClient` with a `Stream*` parameter (`http.sendRequest("POST", &file, fileSize)` or equivalent — no full load into RAM). Set Content-Type `audio/wav`. (FR5, D3, §11 HTTPClient streaming)
  Verify by: code calls `http.sendRequest("POST", &file, fileSize)` with Content-Type `audio/wav`; no buffer of `f.size()` is allocated.
- [ ] 2.7 Add upload metadata headers: `X-Device-Id` (= `REC_HOSTNAME`), `X-Filename` (original basename, e.g. `REC_001.wav`), `X-File-Size` (file size in bytes). (FR5, §5 contract)
  Verify by: three `http.addHeader` calls set `X-Device-Id`=REC_HOSTNAME, `X-Filename`=basename, `X-File-Size`=byte count.
- [ ] 2.8 Acquire `g_spi_bus_mutex` (`extern SemaphoreHandle_t`, created in `m5gfx_lvgl_init`) before SD reads during the upload stream and release it after, same as the SD writer task. (PRD §8, §11 SPI bus mutex)
  Verify by: `xSemaphoreTake(g_spi_bus_mutex, ...)` taken before the SD open/stream and `xSemaphoreGive` after, guarded by a null check like the writer.
- [ ] 2.9 Gate the entire upload cycle on idle + connected: run only when the `recording` flag is false and WiFi is connected. Never attempt upload during recording or while WiFi is off. (FR7, FR13, D6, SC6)
  Verify by: upload cycle is called only inside the `started && !recording` branch in `loop()`; the `recording` early-return at the top of `loop()` still fires first.

### Ack & Integrity (FR8–FR10, D4, D5)

- [ ] 2.10 On a `201 Created` response, parse the JSON body and read `bytes_written`. If it matches the file size sent, ack the file via the existing `ackFile()` (appends to `UPLOADED.txt`). (FR8, D4, D5)
  Verify by: on `code == 201` the response body is parsed for `bytes_written`; `ackFile(name)` called only when the parsed value equals the sent `fileSize`.
- [ ] 2.11 If `bytes_written` does not match the sent size, or the response is any non-201 status / network error, do NOT ack — leave the file for retry on the next cycle. (FR8, FR9)
  Verify by: no `ackFile()` call on any path where `code != 201` or `bytes_written != fileSize`.
- [ ] 2.12 Do not pre-check whether the server already has the file; re-POST of the same device-id + filename is safe (server is idempotent). (FR10, §5 idempotency)
  Verify by: upload path contains no HEAD/GET existence check before POST.

### Retry, Backoff & WiFi Lifecycle (FR11–FR14, D1, D6)

- [ ] 2.13 Rename the `NOTIFY_RETRY_MS` (30 s) constant to `UPLOAD_RETRY_MS` and reuse it as the backoff between upload cycles after a failed upload. (FR11, D6)
  Verify by: `#define UPLOAD_RETRY_MS 30000UL` present; no `NOTIFY_RETRY_MS` remains; backoff gate uses `UPLOAD_RETRY_MS`.
- [ ] 2.14 After all un-acked files are uploaded or all attempts fail, return to the normal idle loop; re-run the upload cycle on the next backoff tick, on recording stop, and on boot. (FR12)
  Verify by: `uploadCycle()` returns control to `loop()` each call; a pending flag (set on boot/recording-stop) or `millis() - last_upload > UPLOAD_RETRY_MS` triggers re-run.
- [ ] 2.15 Remove `notifyHeadspace()` entirely (function, its JSON-notify POST, and the `notify_enabled()`/`HEADSPACE_NOTIFY_URL` notify path). Wire the upload cycle into the same idle window where `notifyHeadspace()` previously ran in `RecorderServer::loop()` (lines ~106–110). (FR14, §7, D1, SC7)
  Verify by: grep shows no `notifyHeadspace` symbol anywhere; upload cycle invoked in the idle branch of `loop()`.
- [ ] 2.16 Remove the pull-triggered ack from `handleDownload()` (the `if (sent == fsize) ackFile(...)` block at line ~198). Ack now occurs only in the upload response handler. Retain the `GET /rec/<name>` and `GET /api/recordings` endpoints for diagnostics. (§7, FR14, SC7)
  Verify by: `handleDownload()` no longer calls `ackFile()`; `/rec/` and `/api/recordings` routes still registered in `routes()`.
- [ ] 2.17 Update `RecorderServer.h` declarations and members accordingly: remove `notifyHeadspace()`, `notify_pending`, `last_notify`, `requestNotify()`; add upload-cycle method(s)/state (e.g. `last_upload`, resolved upload URL). Update any caller of `requestNotify()` (e.g. in `AppRecorder`) to trigger the upload cycle instead. Keep WiFi-OFF-during-recording behaviour and `started = false` reset on the recording→idle transition unchanged. (FR13, FR14, §7)
  Verify by: header has no `notifyHeadspace`/`notify_pending`/`last_notify`; upload members present; both `requestNotify()` call sites in AppRecorder.cpp updated to the upload trigger; `pio run` compiles.

## 3. Testing (Phase 3)

- [ ] 3.1 Compile-first verification: `pio run` exits 0 with the upload cycle in place and `notifyHeadspace()` removed.
- [ ] 3.2 Grep verification that `notifyHeadspace`, `HEADSPACE_NOTIFY_URL`, and the JSON-notify POST are gone; only the upload path remains. (SC7)
- [ ] 3.3 SC1 — bench: a recording completes, the device uploads it, the file appears at `uploads/recordings/core-s3/` on the server with a byte count matching the SD file. (requires companion Headspace endpoint)
- [ ] 3.4 SC2 — mDNS discovery works with `HEADSPACE_UPLOAD_URL` empty (upload succeeds via browse alone). (requires companion mDNS advertisement)
- [ ] 3.5 SC3 — failed upload (server down): device does not ack, retries on the next cycle, succeeds once the server is up.
- [ ] 3.6 SC4 — re-POST of the same file does not create a duplicate (clear ack marker, re-upload).
- [ ] 3.7 SC5 — files already in `UPLOADED.txt` are not re-uploaded after reboot (serial logs show "skipped").
- [ ] 3.8 SC6 — upload never runs during recording (no HTTP activity in serial logs while recording).
- [ ] 3.9 SC8 — fallback to `HEADSPACE_UPLOAD_URL` works when mDNS browse fails (disable mDNS on server, set compile-time URL).

## 4. Final Verification

- [ ] 4.1 All tests passing (compile + applicable SC checks)
- [ ] 4.2 No linter/compiler warnings introduced on the upload path
- [ ] 4.3 Manual verification complete
