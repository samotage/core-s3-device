# Cloud Relay — Workshop Decisions

**Date:** 2026-05-28
**Participants:** Sam (operator), Robbo (architect), Nico (integration), Chip (firmware), Shorty (infrastructure)
**Channel:** #workshop-cloud-recorder-293
**Status:** Deferred — decisions locked, ready to resume when prioritised

---

## Purpose

Take the meeting recorder from LAN-only to cloud-capable. The device (CoreS3 ESP32-S3) currently uses a notify-then-pull pattern: it POSTs a JSON manifest to Headspace on the LAN, Headspace pulls the WAV from the device's HTTP server. Behind a phone hotspot (NAT), the device is unreachable inbound — the pull leg dies. Cloud relay inverts this to device-push so recordings reach Headspace from anywhere with internet.

---

## Pipeline & Upload Model

| ID | Decision |
|----|----------|
| D1 | **Batch push — record to SD, upload after stop.** Single-threaded ESP32 can't serve HTTP + capture mic simultaneously. Battery (200 mAh) favours WiFi-off during recording. Streaming is a different product if ever needed, not a phase of this one. |
| D2 | **Cloud push as production path.** LAN notify-pull retained as compile flag for dev/bench only. One production path to maintain and test. |
| D3 | **Mac-based transcription.** Parakeet-mlx on Apple Silicon. Free, fast, delay-tolerant. No cloud transcription cost. |
| D10 | **No new mobile surface.** Voice PWA + vault is sufficient for viewing transcripts on iPhone. |

## Cloud Endpoint & Storage

| ID | Decision |
|----|----------|
| D4 | **Cloudflare R2 + CF Worker.** Worker generates presigned PUT URLs, device uploads directly to R2. Zero egress fees. Worker + R2 are one platform (~$5/month or less). |
| D5 | **Presigned URL over streaming proxy.** Device talks directly to object storage (handles large files, retries). Worker only handles lightweight URL generation. No body size limits or timeout concerns. Presigned URLs last hours — retry-friendly. |
| D9 | **60-minute presigned URLs, single-key scoped PUT.** Generous for slow hotspot + retry. Short enough that leaked URLs are useless. Even if intercepted, only permits writing one file to one key. |

## Notification & Security

| ID | Decision |
|----|----------|
| D6 | **Tailscale funnel + HMAC for cloud → Headspace notification.** Worker notifies Headspace via Tailscale funnel (`https://<machine>.ts.net/api/recorder/cloud-notify`). HMAC-SHA256 signed payload with `X-Signature-256` header (GitHub webhook convention). Headspace verifies signature + confirms R2 key exists (HEAD) before pulling. |
| D7 | **Static API key auth for device → Worker.** One key compiled into `secrets.h`, sent as Bearer token on the URL-request call only. Presigned URL handles upload auth. No token refresh, no OAuth, no multi-step dance. |
| D8 | **Device acks on upload 2xx only.** Device responsibility ends at R2 confirmation. Downstream (notification, transcription) is not the device's concern. Same ack-to-`UPLOADED.txt` pattern as the LAN path. |

---

## End-to-End Sequence

```
Sam presses STOP
  → Device finalises WAV, flushes to SD
  → Device POSTs to CF Worker: {filename, size, device} + API key
  ← Worker returns {upload_url, key} (presigned R2 PUT, 60 min expiry)
  → Device PUTs WAV directly to R2 via presigned URL
  ← R2 returns 200 → device writes to UPLOADED.txt → device done

R2 event notification fires
  → Worker builds {device, key, filename, size, uploaded_at}
  → Worker HMAC-signs payload, POSTs to Headspace via Tailscale funnel
  ← Headspace verifies HMAC, HEAD-checks R2 key exists
  → Headspace downloads WAV from R2 (S3-compatible GET)
  → Parakeet-mlx transcribes → SRT alongside WAV
  → Post-recording agent spawned with transcript

Sam opens voice PWA → transcript visible
```

**Wall-clock estimate:** Steps 1-3 (device upload) take 1-8 minutes depending on file size and hotspot speed. Notification + pull is seconds. Transcription is ~30 seconds per 10 minutes of audio on Apple Silicon. Realistically: Sam presses stop, pockets the device, transcript is ready by the time he checks his phone.

---

## Integration Contracts

Four boundaries identified and spec'd in `CLOUD_RELAY_API.md`:

| Boundary | Path | Summary |
|----------|------|---------|
| 1a — Request Upload URL | Device → CF Worker | `POST /upload` with `{device, filename, size}` + Bearer token. Returns `{upload_url, key}`. |
| 1b — Upload WAV | Device → R2 (direct) | `PUT` raw WAV bytes to presigned URL. No auth headers (baked into URL). |
| 2b — Cloud Notify | Worker → Headspace | HMAC-signed `{device, key, filename, size, uploaded_at}` via Tailscale funnel. |
| 3 — Pull from R2 | Headspace → R2 | Standard S3 `GetObject` with read-only credentials. HEAD first, then GET. |

**Error taxonomy per boundary:** 401 (bad key, don't retry), 400 (bad payload), 429 (rate limit with `retry_after`), 403 at 1b (URL expired, re-request from 1a), 500/503 (retry 3x at 30s). Dead letter to `_notify_failed/` prefix after 5 notification retries.

---

## Open Items for Resumption

| # | Item | Owner |
|---|------|-------|
| O1 | **Date-partitioned R2 keys** — Worker should prefix keys with date (`core-s3/2026-05-28/REC_004.wav`) to prevent collision on SD card format/counter reset. Fold into contract doc. | Nico |
| O2 | **Periodic dead letter sweep** — Headspace should check `_notify_failed/` prefix every 10-15 minutes (not just on startup). One LIST call, almost always empty. | Con |
| O3 | **Firmware flash budget** — 94.7% of 16 MB app partition used. HTTPS client + multi-network config likely requires trimming AppCamera/factory demo. | Chip |
| O4 | **Multi-network firmware config** — Priority-ordered WiFi network list (home WiFi + hotspot) in `secrets.h`. | Chip |
| O5 | **Contract document finalisation** — `CLOUD_RELAY_API.md` needs O1 and O2 folded in before it's final. | Nico |
| O6 | **Headspace recorder route changes** — New handler (or mode switch) to pull from R2 instead of LAN IP. | Con |
