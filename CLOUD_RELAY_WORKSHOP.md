# Cloud Relay Workshop Summary

**Channel:** #workshop-cloud-recorder-293
**Date:** 2026-05-28
**Participants:** Robbo (architecture), Nico (integration), Chip (firmware), Shorty (infrastructure)
**Status:** Deferred — decisions locked, ready to resume when prioritised

## Problem

The meeting recorder (CoreS3 ESP32-S3) only works on Sam's home LAN. The device notifies Headspace, Headspace pulls the WAV from the device's HTTP server. Behind a phone hotspot (NAT), the device is unreachable inbound — the pull leg dies. Sam needs recordings to reach Headspace from anywhere with internet.

## Decisions Locked

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | **Batch push** — record to SD, upload after stop | Single-threaded ESP32 can't serve HTTP + capture mic simultaneously. Battery (200 mAh) favours WiFi-off during recording. |
| D2 | **Cloud-only production path** | LAN notify-pull retained as compile flag for dev/bench. One production path to maintain and test. |
| D3 | **Mac-based transcription** | Parakeet-mlx on Apple Silicon. Free, fast, delay-tolerant. No cloud transcription cost. |
| D4 | **Cloudflare R2 + CF Worker** | Worker generates presigned PUT URLs, device uploads directly to R2. Zero egress fees. Worker + R2 are one platform. ~$5/month or less. |
| D5 | **Presigned URL over streaming proxy** | Device talks directly to object storage (handles large files, retries). Worker only handles lightweight URL generation. No body size limits or timeout concerns. |
| D6 | **Tailscale funnel + HMAC for boundary 2** | Worker notifies Headspace via funnel. HMAC-SHA256 signed payload (GitHub webhook convention). Headspace verifies + confirms R2 key exists before pulling. |
| D7 | **Static API key auth** | One key in `secrets.h`, Bearer token on URL-request call only. Presigned URL handles upload auth. No token refresh, no OAuth. |
| D8 | **Device acks on upload 2xx** | Device responsibility ends at R2 confirmation. Downstream (notification, transcription) is not the device's concern. |
| D9 | **60-minute presigned URLs** | Single-key scoped PUT. Generous for slow hotspot + retry. Short enough that leaked URLs are useless. |
| D10 | **No new mobile surface** | Voice PWA + vault is sufficient for viewing transcripts. |

## Architecture — End-to-End Sequence

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

## Integration Contracts (Nico)

Four boundaries identified and spec'd:

- **1a — Request Upload URL:** Device → CF Worker. `POST /upload` with Bearer token. Returns presigned PUT URL.
- **1b — Upload WAV:** Device → R2 (direct). `PUT` with raw WAV body. No auth headers (baked into URL).
- **2b — Cloud Notify:** Worker → Headspace via Tailscale funnel. HMAC-signed JSON payload.
- **3 — Pull from R2:** Headspace → R2. Standard S3 `GetObject` with read-only credentials.

Error taxonomy per boundary: 401 (bad key, don't retry), 400 (bad payload), 429 (rate limit with `retry_after`), 403 at 1b (URL expired, re-request from 1a), 500/503 (retry 3x at 30s). Dead letter to `_notify_failed/` prefix after 5 notification retries.

## Open Items for Resumption

1. **Date-partitioned R2 keys** — Worker should prefix keys with date (`core-s3/2026-05-28/REC_004.wav`) to prevent collision on SD card format/counter reset. Nico to fold into contract doc.
2. **Periodic dead letter sweep** — Headspace should check `_notify_failed/` prefix every 10-15 minutes (not just on startup) to catch "Mac was asleep then woke up" case. One LIST call, almost always empty.
3. **Firmware flash budget** — Chip flagged 94.7% of 16 MB app partition used. Adding HTTPS client + multi-network config likely requires trimming AppCamera/factory demo. Chip's call on what to cut.
4. **Multi-network firmware config** — Device needs priority-ordered WiFi network list (home WiFi + hotspot) in `secrets.h`. Implementation details deferred.
5. **Full contract document** — Nico drafted `CLOUD_RELAY_API.md` with payloads, error cases, state machine. Needs the two review findings folded in before it's final.
6. **Headspace recorder route changes** — New handler (or mode switch) to pull from R2 instead of LAN IP. Con's lane when we resume.
