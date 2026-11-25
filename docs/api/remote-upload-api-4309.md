# Remote Upload API — CoreS3 recorder → Headspace via Tailscale Funnel (#4309)

Integration contract between the CoreS3 firmware (Chip) and Headspace (Con).
Status: draft for channel brief.

## Scenario

Sam is out (client office) with the recorder and iPhone. The laptop running
Headspace is **at home, awake, on home WiFi — not on the hotspot**. The device
joins the iPhone Personal Hotspot (built: core-s3 `fe9d500`) and must upload
recordings to Headspace over the internet so transcription and analysis start
on the trip home.

Route: device → HTTPS → `https://smac.griffin-blenny.ts.net:8443` (Tailscale
Funnel, already live) → nginx public listener `127.0.0.1:18443` → Headspace
`127.0.0.1:15055`. Tailscale can't run on the ESP32; Funnel is the route.

At home the existing LAN path (`POST /api/uploads/recordings`, mDNS discovery,
plain HTTP) is unchanged.

## Security model

- **Per-device recorder token**, sent as `Authorization: Bearer <token>`.
  Validated **before the body is read**. Missing/invalid → 401, nothing written.
- Token is **never compiled into firmware** (core-s3 repo is public). It is
  **vended by Headspace over the home LAN only** (endpoint R0 below), and the
  device stores it in NVS.
- Token scope: the recorder routes R1–R3 only. Refused everywhere else, same
  closed-world rule as the voice device token (`services/device_access.py`).
  **Decided:** reuse the Tab5 `devices` table (hashed storage, revoke,
  reissue) with a new `kind` column (`voice` | `recorder`); each kind's token
  is refused on the other kind's routes.
- Revocable and rotatable from Headspace. Re-vend replaces the old token.
- nginx: R1–R3 added to the public listener allow-list
  (`services/nginx_public_listener.py`), with a `limit_req` rate limit and
  `client_max_body_size` = part size + headroom (2 MB). Every other path stays 404.
- Client certificates are not possible — Funnel terminates TLS.

## Endpoints

### R0 — Vend token (LAN / tailnet only, NEVER on the public listener)

`POST /api/recorder/device/token`

Headers: `X-Device-Id: core-s3`

- Public-listener request → 403 `{"error": "lan_only"}`.
- **Pairing window (decided):** opened from Headspace CLI or UI, lasts 5 min,
  issues one token, then closes. Outside it → 403 `{"error": "pairing_closed"}`.
  A known device id proves nothing (it's in a public repo).
- Device matches on the `error` field, not the status code. Pairing is
  triggered by a "Pair" button on the device Settings page.

201:
```json
{"device_id": "core-s3", "token": "<opaque>", "issued_at": "2026-11-25T09:30:00+11:00",
 "public_base_url": "https://smac.griffin-blenny.ts.net:8443"}
```

The device calls R0 on the LAN when it has no token, or when R1–R3 return 401.
`public_base_url` lets Headspace move the Funnel without a firmware change.

### R1 — Start or resume an upload

`POST /api/recorder/uploads`

Headers: `Authorization: Bearer <token>`, `X-Device-Id`, `Content-Type: application/json`

```json
{"filename": "REC_042.wav", "total_size": 115200044}
```

200/201:
```json
{"upload_id": "<opaque>", "offset": 0, "part_size": 1048576}
```

Idempotent on (device_id, filename, total_size): an existing incomplete upload
returns its `upload_id` and **current confirmed offset**, so the device resumes
after a dropout or reboot. A completed match returns `{"complete": true}` and the
device acks without re-sending.

### R2 — Append a part

`PUT /api/recorder/uploads/<upload_id>/parts?offset=<N>`

Headers: `Authorization`, `X-Device-Id`, `Content-Type: application/octet-stream`,
`Content-Length` (≤ `part_size`)

- `offset` must equal the server's confirmed offset. Mismatch → 409 with
  `{"offset": <server offset>}`; the device seeks there and continues.
- Server appends and fsyncs before replying.

200: `{"offset": <new confirmed offset>}`

### R3 — Complete

`POST /api/recorder/uploads/<upload_id>/complete`

- Verify the assembled size equals `total_size`, then do exactly what the LAN
  path does: atomic move into the staging dir, upsert the `Recording` row,
  create the tracking task, trigger transcription.

201: `{"filename": "REC_042.wav", "bytes_written": 115200044}`

The device acks to `UPLOADED.txt` **only** when `bytes_written == total_size`.
That's the same rule as the LAN path.

## Error taxonomy (device behaviour)

| Code | Meaning | Device action |
|------|---------|---------------|
| 401 | Bad/missing/revoked token | Stop remote uploads; re-vend at home (R0). Show on Settings page. |
| 409 | Offset mismatch | Seek to returned offset, continue |
| 413 | Part too large | Bug — log, drop to server `part_size` |
| 429 | Rate limited | Back off `Retry-After` |
| 5xx / transport | Server/laptop/network | Retry same offset with backoff |

## Sizes

16 kHz × 16-bit mono = 32 KB/s ≈ 115 MB/h. Firmware hard cap is 2 h
(`REC_MAX_SECONDS`), so the largest file is ~230 MB. No whole-file cap is
needed; the per-request cap is the part size.

## Housekeeping

- Incomplete uploads older than 7 days are purged.
- Logs: one line per part at debug, one per start/complete at info.

## Firmware side (Chip) — for reference

- HTTPS client (mbedTLS), public base URL from R0, token in NVS.
- Remote path used only when LAN discovery finds no Headspace.
- Settings page "Remote upload": token present/absent, issued-at, last result.
