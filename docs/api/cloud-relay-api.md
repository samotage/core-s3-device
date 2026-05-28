# Cloud Relay — Integration Contracts

The contract between the CoreS3 device, Cloudflare (R2 + Workers), and Headspace
for cloud-based recording upload. Replaces the LAN notify-pull pipeline when the
device is off the home network.

**Architecture decision:** Batch push. Device records to SD, uploads after stop.
Cloud notifies Headspace, Headspace pulls from R2 and transcribes locally.

**Supersedes:** The LAN notify-pull contract in `device-api.md` remains valid for
local dev/debug (compile flag). This document defines the production cloud path.

---

## System Boundaries

```
Device ──(1a)──▶ CF Worker     (request presigned URL)
Device ──(1b)──▶ R2            (upload WAV via presigned PUT)
R2     ──(2a)──▶ CF Worker     (R2 event notification, internal)
CF Worker ─(2b)─▶ Headspace    (cloud notify via Tailscale funnel)
Headspace ─(3)──▶ R2           (pull WAV for transcription)
```

Three external contracts (1a, 1b, 2b) and one internal (2a, R2→Worker binding).
Contract 3 is Headspace-initiated and uses the S3-compatible R2 API.

---

## Boundary 1a — Request Upload URL

**Device → CF Worker**

The device requests a presigned PUT URL for uploading a recording.

### Request

```
POST https://recorder.otagelabs.com/upload
Content-Type: application/json
Authorization: Bearer <DEVICE_API_KEY>
```

```json
{
  "device": "core-s3",
  "filename": "REC_004.wav",
  "size": 19200044
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `device` | string | yes | Device identifier. Used as R2 key prefix. |
| `filename` | string | yes | WAV basename. Must match `REC_*.wav`. |
| `size` | integer | yes | File size in bytes. Worker may reject files above a configured max (default 200 MB). |

### Response — 200 OK

```json
{
  "upload_url": "https://<r2-bucket>.r2.cloudflarestorage.com/core-s3/REC_004.wav?X-Amz-...",
  "key": "core-s3/REC_004.wav",
  "expires_in": 3600
}
```

| Field | Type | Description |
|---|---|---|
| `upload_url` | string | Presigned R2 PUT URL. Single-key scoped. Valid for `expires_in` seconds. |
| `key` | string | R2 object key. Format: `{device}/{filename}`. |
| `expires_in` | integer | URL validity in seconds. Default 3600 (60 minutes). |

### Error Responses

| Status | Body | Meaning | Device action |
|---|---|---|---|
| `401` | `{"error": "unauthorized"}` | Missing or invalid API key. | Log error. Do not retry (key is wrong, not transient). |
| `400` | `{"error": "invalid_request", "detail": "..."}` | Bad payload — missing field, invalid filename pattern, size exceeds max. | Log error. Do not retry (payload is malformed). |
| `429` | `{"error": "rate_limited", "retry_after": <seconds>}` | Too many requests. | Wait `retry_after` seconds, then retry. |
| `500` | `{"error": "internal"}` | Worker-side failure. | Retry after 30s backoff. Max 3 retries. |

### Auth

Static API key compiled into `secrets.h` on the device. Sent as `Authorization: Bearer <key>`.
The Worker validates the key against a Workers secret (`DEVICE_API_KEY`). No token refresh,
no OAuth, no multi-step handshake.

**Key rotation:** Deploy a new key to the Worker secret, flash new firmware. No overlap
window needed — the old key stops working immediately. At current device count (1), this
is a non-issue.

---

## Boundary 1b — Upload WAV

**Device → R2 (via presigned URL)**

The device uploads the WAV file directly to R2. No Worker in the data path.

### Request

```
PUT <upload_url from Boundary 1a>
Content-Type: audio/wav
Content-Length: <size>
```

Body: raw WAV file bytes.

No `Authorization` header — auth is embedded in the presigned URL signature.

### Response — 200 OK

Empty body. The upload succeeded. R2 returns standard S3-compatible PUT response headers
(`ETag`, `x-amz-request-id`). The device does not need to parse them.

### Error Responses

| Status | Meaning | Device action |
|---|---|---|
| `200` | Upload succeeded. | Write filename to `/UPLOADED.txt`. **Device is done.** |
| `400` | Bad request (malformed, content-length mismatch). | Log error. Request a new presigned URL and retry. |
| `403` | Presigned URL expired or tampered. | Request a new presigned URL (Boundary 1a) and retry. |
| `500`/`503` | R2 server error. | Retry the same PUT after 30s. Max 3 retries. If all fail, request a new URL. |

### Upload Behaviour

- **Content-Length required — no chunked transfer.** Presigned PUT URLs include
  `Content-Length` in the signature. Sending `Transfer-Encoding: chunked` instead will
  cause `SignatureDoesNotMatch` (403). The ESP32 HTTP client must set `Content-Length`
  explicitly from the known file size and disable chunked encoding.
- **Idle timeout, not total timeout.** Device should set a socket *idle* timeout of
  120 seconds (no data sent or received for 120s = stalled connection, abort). The
  total transfer time for a 115 MB file on a slow hotspot (1 Mbps) is ~15 minutes —
  a total timeout would kill legitimate slow uploads.
- **Resume:** Not supported in Phase 2a. A failed upload restarts from the beginning with a
  new presigned URL. R2 multipart upload can be added later if partial uploads become a
  problem at larger file sizes.
- **Ack semantics:** Device writes filename to `/UPLOADED.txt` on SD **only** on 200 from the
  PUT. Same file, same semantics as the LAN pipeline. Un-acked files are retried on the
  next 30s tick.
- **Failed upload observability.** On terminal upload failure (retries exhausted), the
  device displays the filename and error on screen and keeps it in the un-acked set.
  Files that permanently fail upload are never written to `/UPLOADED.txt` and will
  appear in every upload cycle until the device is reflashed or the file is manually
  removed from SD. At current volume (one device, operator is the user), screen
  display is sufficient — no remote alerting needed in Phase 2a.

---

## Boundary 2a — R2 Event Notification (internal)

**R2 → CF Worker (R2 event binding)**

This is not an HTTP contract — it's a Cloudflare-internal event binding configured in
`wrangler.toml`. Included here for completeness.

### Trigger

R2 `object-create` event on the recordings bucket. Fires when a PUT completes
successfully (i.e., after Boundary 1b succeeds).

### Event Payload (Cloudflare-provided)

```json
{
  "object": {
    "key": "core-s3/REC_004.wav",
    "size": 19200044,
    "etag": "..."
  },
  "bucket": "recorder-uploads",
  "action": "PutObject"
}
```

The Worker extracts `key` and parses `{device}/{filename}` from it.

---

## Boundary 2b — Cloud Notify

**CF Worker → Headspace (via Tailscale funnel)**

The Worker notifies Headspace that a new recording is available in R2. This replaces
the device's direct LAN POST to `/api/recorder/notify`.

### Request

```
POST https://<machine>.tail-net-name.ts.net/api/recorder/cloud-notify
Content-Type: application/json
X-Signature-256: sha256=<HMAC hex digest>
```

```json
{
  "device": "core-s3",
  "key": "core-s3/REC_004.wav",
  "filename": "REC_004.wav",
  "size": 19200044,
  "uploaded_at": "2026-05-28T14:30:00Z"
}
```

| Field | Type | Description |
|---|---|---|
| `device` | string | Device identifier. |
| `key` | string | R2 object key. Headspace uses this to pull the file. |
| `filename` | string | Original WAV basename. Used for local storage naming. |
| `size` | integer | File size in bytes. Headspace can use this to verify the download. |
| `uploaded_at` | string | ISO 8601 UTC timestamp of the R2 upload. |

### HMAC Signature

The Worker signs the raw JSON request body with HMAC-SHA256 using a shared secret
(`NOTIFY_HMAC_SECRET`, stored as a Workers secret and in Headspace's `config.yaml`).

```
signature = HMAC-SHA256(secret, raw_request_body)
header = "sha256=" + hex(signature)
```

**Verification in Headspace (Python):**

```python
import hmac, hashlib

expected = hmac.new(
    secret.encode(), request.data, hashlib.sha256
).hexdigest()
received = request.headers["X-Signature-256"].removeprefix("sha256=")
if not hmac.compare_digest(expected, received):
    abort(401)
```

The header name and format follow the GitHub webhook convention (`X-Hub-Signature-256`
pattern) — well-understood, easy to verify, constant-time comparison.

### Response — 200 OK

```json
{
  "status": "accepted",
  "key": "core-s3/REC_004.wav"
}
```

Headspace acknowledges receipt. Processing (download, transcription, agent spawn) happens
asynchronously — the Worker does not wait for it.

### Error Responses

| Status | Meaning | Worker action |
|---|---|---|
| `200` | Accepted. | Done. |
| `401` | HMAC verification failed. | Log error. Do not retry (secret mismatch is not transient). |
| `404` | Endpoint not found. | Log error. Headspace may not have the route deployed. |
| `500`/`503` | Headspace error. | Retry with exponential backoff: 10s, 30s, 120s. Max 3 retries. |
| Timeout / unreachable | Mac is asleep or offline. | Retry with exponential backoff: 30s, 120s, 600s. Max 5 retries. After exhaustion, write to a dead-letter key in R2 (`_notify_failed/core-s3/REC_004.wav.json`) for manual recovery. |

### Dead Letter Recovery

If all notification retries are exhausted (Mac was off for hours), the Worker writes
the notification payload to `_notify_failed/{key}.json` in R2. Headspace can run a
sweep on startup: list `_notify_failed/` prefix, process each, delete after success.

This handles the "Sam recorded at a client site, Mac was off, he gets home hours later
and turns it on" scenario without losing recordings.

---

## Boundary 3 — Headspace Pulls from R2

**Headspace → R2 (S3-compatible API)**

Headspace downloads the WAV from R2 for local transcription. This is not a custom
contract — it uses the standard S3 `GetObject` API via an S3-compatible client.

### Credentials

R2 API token with read-only access to the recordings bucket. Stored in Headspace's
`config.yaml` under the `recorder.cloud` section:

```yaml
recorder:
  cloud:
    r2_endpoint: "https://<account_id>.r2.cloudflarestorage.com"
    r2_bucket: "recorder-uploads"
    r2_access_key_id: "<key>"
    r2_secret_access_key: "<secret>"
```

### Download Flow

1. Receive notification (Boundary 2b) with `key`.
2. `HEAD` the key — confirm it exists and `Content-Length` matches `size` from notification.
3. `GET` the key — download to `uploads/recordings/{device}/{filename}`.
4. Verify downloaded file size matches expected.
5. Proceed to transcription (Parakeet-mlx).

### Post-Download Cleanup

After successful transcription, Headspace may optionally delete the R2 object to
keep storage clean. Deferred to Phase 2b — for now, files accumulate in R2 (well
within the 10 GB free tier at current volume).

---

## Device Upload State Machine

```
                    ┌─────────┐
                    │  IDLE   │
                    └────┬────┘
                         │ recording stopped, un-acked files exist
                         ▼
              ┌─────────────────────┐
              │  REQUEST UPLOAD URL │──── 401/400 ──▶ LOG + STOP
              │  POST /upload       │──── 429 ──────▶ WAIT retry_after
              └──────────┬──────────┘──── 500 ──────▶ RETRY (3x, 30s backoff)
                         │ 200 + upload_url
                         ▼
              ┌─────────────────────┐
              │  UPLOAD WAV         │──── 403 ──────▶ REQUEST UPLOAD URL (re-request)
              │  PUT <presigned>    │──── 500/503 ──▶ RETRY (3x, 30s backoff)
              └──────────┬──────────┘──── timeout ──▶ REQUEST UPLOAD URL (new URL)
                         │ 200
                         ▼
              ┌─────────────────────┐
              │  ACK                │
              │  write UPLOADED.txt │
              └──────────┬──────────┘
                         │ more un-acked files?
                    yes ──┘          no ──▶ IDLE (wait 30s, check again)
```

The device processes one file at a time. On any terminal failure (auth error, malformed
request), it stops retrying that file and moves to the next. Files that permanently
fail upload are never acked and will appear in every notification cycle — a human will
eventually notice.

---

## Configuration Summary

### CF Worker Secrets

| Secret | Purpose |
|---|---|
| `DEVICE_API_KEY` | Static bearer token for Boundary 1a auth. |
| `NOTIFY_HMAC_SECRET` | Shared secret for Boundary 2b HMAC signing. |
| `R2_BUCKET` | Bucket name (could also be in `wrangler.toml`). |
| `HEADSPACE_NOTIFY_URL` | Tailscale funnel URL for Boundary 2b. |

### Device (`secrets.h`)

```c
#define CLOUD_UPLOAD_URL    "https://recorder.otagelabs.com/upload"
#define CLOUD_API_KEY       "<device-api-key>"
// WiFi networks (tried in order)
#define WIFI_SSID_1         "<home-wifi>"
#define WIFI_PASS_1         "<home-password>"
#define WIFI_SSID_2         "<hotspot-ssid>"
#define WIFI_PASS_2         "<hotspot-password>"
```

### Headspace (`config.yaml`)

```yaml
recorder:
  storage_dir: uploads/recordings
  transcription_model: parakeet-mlx
  cloud:
    enabled: true
    r2_endpoint: "https://<account_id>.r2.cloudflarestorage.com"
    r2_bucket: "recorder-uploads"
    r2_access_key_id: "<key>"
    r2_secret_access_key: "<secret>"
    hmac_secret: "<notify-hmac-secret>"
```

---

## Audio Format (unchanged)

16 kHz · mono · 16-bit PCM WAV. Same as the LAN pipeline. No transcoding at any
boundary. The WAV that lands on the SD card is byte-identical to the WAV that
Headspace transcribes.

---

## Migration from LAN Pipeline

The cloud pipeline does **not** replace the LAN pipeline — both can coexist.

- **LAN path** (`device-api.md`): Device notifies Headspace directly on LAN, Headspace
  pulls from device. Used for dev/debug with a compile flag.
- **Cloud path** (this document): Device pushes to R2, Worker notifies Headspace,
  Headspace pulls from R2. Production path.

Headspace needs two recorder routes:
- `POST /api/recorder/notify` — existing LAN handler (unchanged).
- `POST /api/recorder/cloud-notify` — new cloud handler (Boundary 2b).

Both feed into the same downstream pipeline: save WAV → run Parakeet → spawn agent.

---

## Phase 2a Scope

In scope:
- All boundaries above (1a, 1b, 2a, 2b, 3)
- Single device (`core-s3`)
- Batch upload (post-recording)
- Static API key auth
- Dead letter recovery for missed notifications

Out of scope (Phase 2b+):
- Multipart/resumable upload for very large files
- R2 object lifecycle (auto-delete after N days)
- Multiple devices
- Real-time / chunked streaming
- Cloud-based transcription
- Upload progress on device screen
