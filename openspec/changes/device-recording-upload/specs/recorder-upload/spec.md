## ADDED Requirements

### Requirement: mDNS Service Discovery of Upload Endpoint
On WiFi connect the device SHALL browse for the mDNS service `_otl-recordings._tcp` (via `MDNS.queryService("otl-recordings", "tcp")`, without leading underscores), extract the host IP and port from the first service record, and construct the upload URL `http://<ip>:<port>/api/uploads/recordings`. The browse SHALL be re-attempted on each WiFi reconnect, because the device tears down and re-initialises mDNS on every recording→idle transition and the DHCP lease may have changed the server's IP. This change discovers the service only; it does not advertise it.

#### Scenario: Discovery succeeds on WiFi connect
- **WHEN** WiFi associates and the device browses for `_otl-recordings._tcp`
- **THEN** the device reads host, IP, and port from the first service record and constructs the upload URL from them

#### Scenario: Re-browse after recording→idle transition
- **WHEN** a recording stops, WiFi re-enables, and mDNS is re-initialised
- **THEN** the device re-runs the service browse rather than reusing a stale cached result

---

### Requirement: Compile-Time Fallback Upload URL
The device SHALL fall back to a compile-time `HEADSPACE_UPLOAD_URL` defined in `secrets.h` when the mDNS browse returns no results or times out. When mDNS resolves, it SHALL take priority over the fallback. When neither mDNS nor a configured fallback URL is available, uploads SHALL be disabled and a serial log message SHALL be emitted. The legacy `HEADSPACE_NOTIFY_URL` is replaced by `HEADSPACE_UPLOAD_URL` using the same conditional-include pattern.

#### Scenario: Fallback used when mDNS fails
- **WHEN** the mDNS browse returns 0 results and `HEADSPACE_UPLOAD_URL` is configured
- **THEN** the device uses the configured fallback URL for the upload

#### Scenario: Uploads disabled when no endpoint resolvable
- **WHEN** the mDNS browse fails and `HEADSPACE_UPLOAD_URL` is empty
- **THEN** uploads are disabled and a serial log message indicates the failure

#### Scenario: mDNS takes priority over fallback
- **WHEN** both an mDNS result and a configured `HEADSPACE_UPLOAD_URL` are available
- **THEN** the device uses the mDNS-derived URL

---

### Requirement: Device-Push WAV Upload
When idle and WiFi-connected, the device SHALL iterate all `REC_*.wav` files on the SD card, skip any already acked in `UPLOADED.txt`, and for each un-acked file perform an HTTP `POST` to `/api/uploads/recordings` at the resolved endpoint. The POST body SHALL be the raw WAV bytes streamed off SD via `HTTPClient` with a `Stream*` parameter (not loaded into RAM, not multipart-encoded), with Content-Type `audio/wav`. Files SHALL be uploaded sequentially, oldest first, with no concurrent uploads. The request SHALL include metadata headers `X-Device-Id` (the device hostname), `X-Filename` (the original basename), and `X-File-Size` (file size in bytes). The SD reads during the upload SHALL acquire the shared SPI-bus mutex.

#### Scenario: Sequential upload of un-acked files
- **WHEN** the device is idle with WiFi connected and multiple un-acked `REC_*.wav` files are on SD
- **THEN** the device uploads them one at a time, oldest first, skipping files already in `UPLOADED.txt`

#### Scenario: Raw streamed body with metadata headers
- **WHEN** the device POSTs a recording
- **THEN** the body is the raw WAV bytes streamed from SD with Content-Type `audio/wav` and headers `X-Device-Id`, `X-Filename`, and `X-File-Size` set

#### Scenario: SPI-bus mutex held for SD reads
- **WHEN** the upload streams a file off the SD card while the LCD may also access the shared SPI bus
- **THEN** the upload path acquires the shared SPI-bus mutex before reading from SD and releases it afterward

---

### Requirement: Confirmed-Disk-Write Ack
The device SHALL ack a file only when the server returns `201 Created` AND the `bytes_written` field in the JSON response body matches the number of bytes sent. On a matching 201 the file SHALL be acked via the existing `ackFile()`/`UPLOADED.txt` mechanism. On any non-201 response, a network error, or a `bytes_written` mismatch, the device SHALL NOT ack and the file SHALL be retried on the next upload cycle. The device SHALL NOT pre-check whether the server already holds the file, because re-POST of the same device-id + filename is idempotent on the server.

#### Scenario: Ack on confirmed 201 with matching byte count
- **WHEN** the server responds `201 Created` with `bytes_written` equal to the file size sent
- **THEN** the device acks the file via `ackFile()` (appends to `UPLOADED.txt`)

#### Scenario: No ack on byte-count mismatch
- **WHEN** the server responds `201` but `bytes_written` does not match the sent size
- **THEN** the device does not ack the file and retries it on the next cycle

#### Scenario: No ack on non-201 or network error
- **WHEN** the upload returns any non-201 status or fails with a network error
- **THEN** the device does not ack the file and retries it on the next cycle

#### Scenario: Idempotent re-upload
- **WHEN** the same file (same device-id + filename) is re-POSTed after a prior failure
- **THEN** the device sends it without pre-checking server state, relying on server-side idempotency to avoid duplicates

---

### Requirement: Idle-Only Upload with Backoff Retry
The upload cycle SHALL run only when the device is not recording and WiFi is connected; it SHALL never run during recording or while WiFi is off. WiFi SHALL remain OFF during recording (existing behaviour) and re-enable on recording stop, after which uploads begin once the connection is re-established. After a failed upload the device SHALL wait `UPLOAD_RETRY_MS` (30 s, reusing the existing backoff constant) before the next attempt. After all un-acked files are uploaded or all attempts fail, the device SHALL return to its idle loop; the upload cycle SHALL re-run on the next backoff tick, on recording stop, and on boot.

#### Scenario: Upload suppressed during recording
- **WHEN** a recording is active
- **THEN** no upload is attempted and no HTTP activity occurs until recording stops and WiFi reconnects

#### Scenario: Backoff between failed attempts
- **WHEN** an upload fails
- **THEN** the device waits `UPLOAD_RETRY_MS` (30 s) before the next upload attempt

#### Scenario: Cycle re-runs on the standard triggers
- **WHEN** the device finishes or exhausts an upload cycle
- **THEN** the cycle re-runs on the next backoff tick, on recording stop, and on boot

---

### Requirement: Replacement of the Notify-Then-Pull Flow
The device SHALL push recordings directly to Headspace, superseding the notify-then-pull flow. The `notifyHeadspace()` function and its JSON-notification POST SHALL be removed, replaced by the upload cycle. The pull-triggered ack in `handleDownload()` SHALL be removed; ack now occurs only in the upload response handler. The diagnostic endpoints `GET /api/recordings` and `GET /rec/<name>` SHALL be retained.

#### Scenario: Notify path removed
- **WHEN** the firmware runs after this change
- **THEN** `notifyHeadspace()` is absent, no JSON notification POST is sent, and `HEADSPACE_NOTIFY_URL` is no longer referenced

#### Scenario: Ack no longer fires on pull
- **WHEN** a recording is served via `GET /rec/<name>`
- **THEN** serving the file does not ack it; ack happens only via the confirmed-disk-write upload response

#### Scenario: Diagnostic endpoints retained
- **WHEN** a client requests `GET /api/recordings` or `GET /rec/<name>`
- **THEN** the device still serves the listing and the file (for diagnostics and manual access)
