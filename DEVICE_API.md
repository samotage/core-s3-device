# CoreS3 Recorder — Device HTTP API

The contract for the ingest agent (Mick). The device is an HTTP **server**; the
agent is the **client** that discovers it, lists recordings, and downloads them.

## Discovery

- **mDNS hostname:** `core-s3.local` (configurable via `REC_HOSTNAME` in `secrets.h`)
- **Service:** `_http._tcp` on port `80`, TXT `path=/api/recordings`
- **Base URL:** `http://core-s3.local`
- The device must be powered on and joined to the same WiFi as the agent.
  Practical flow: bring the device back to the desk, plug it in (USB power),
  it auto-joins WiFi → the agent can reach it.

## Endpoints

### `GET /api/recordings`
List the recordings currently on the SD card.

```json
[
  {"name": "REC_001.wav", "size": 1920044},
  {"name": "REC_002.wav", "size": 5760044}
]
```

- `name` — filename at the SD root. Sequential, zero-padded (`REC_NNN.wav`).
- `size` — bytes (includes the 44-byte WAV header).

### `GET /rec/<name>`
Download one recording. `<name>` must be a `REC_*.wav` basename (no paths).

- `200` → body is the WAV (`Content-Type: audio/wav`,
  `Content-Disposition: attachment`).
- `400` → bad/unsafe name. `404` → not found.

### `GET /`
Plain-text help page (human sanity check).

## Recording-in-progress guard

While a recording is active, **every endpoint returns `503`** (body
`{"error":"recording"}` for the JSON route). This is deliberate: the device is
single-threaded, so serving a download would stall the real-time capture loop.
The agent should treat `503` as "busy, retry later".

## Audio format

16 kHz · mono · 16-bit PCM WAV. Whisper-ready — no res/transcode needed.

## Suggested ingest loop (Mick)

1. Resolve `core-s3.local`; if unreachable, the device is off/away — skip.
2. `GET /api/recordings`. On `503`, back off and retry.
3. For each entry not already ingested (dedupe on `name` + `size`):
   `GET /rec/<name>` → save locally → transcribe → store transcript alongside
   the audio → mark `name` ingested.
4. Deletion is **not** exposed yet — the device keeps recordings until the SD is
   cleared manually. Mick owns the ingested-set so files aren't re-processed.
   (A `DELETE`/move-to-fetched endpoint can be added once the pipeline is trusted.)

## Notes / future

- No auth (trusted LAN assumption). Add a shared-token header if the device ever
  sits on an untrusted network.
- Transcription engine (local `faster-whisper` vs Whisper API) is Mick's choice
  — deferred, does not affect this contract.
