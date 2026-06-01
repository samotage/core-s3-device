Workshop: Recorder Transcription Pipeline — Headspace Side

Context: The CoreS3 recorder device captures meeting audio (16 kHz mono WAV, single mic) and pushes files to a Headspace REST endpoint. A companion PRD in the core-s3 repo (docs/prds/recorder/device-recording-upload-prd.md, committed a8633d3) covers the device-side push and sent-tracking. This workshop covers everything from the moment the file arrives at Headspace through to a finished transcript on disk.

What already exists:
- Upload directory: uploads/recordings/ (WAV files have landed here before, from an earlier pull-based mechanism)
- Transcript destination: data/transcripts-recordings/ (exists, currently empty)
- recorder_bp in routes/recorder.py — existing recorder route blueprint, natural home for the upload endpoint. Already handles device notifications and recording storage.
- Deepgram SDK — already integrated for streaming STT in the voice pipeline. Batch file transcription is a different API and likely new work.
- Headspace runs on port 15055 on LAN (not 5055 — that's the Tailscale URL)

Input contract (agreed in core-s3 workshop #315, shared with device PRD):
- POST /api/uploads/recordings — body is raw WAV bytes, Content-Type: audio/wav (not multipart — device streams straight off SD)
- Headers carry metadata: X-Device-Id (core-s3), X-Filename (REC_NNN.wav), X-File-Size (bytes)
- Audio format: 16 kHz mono 16-bit PCM WAV. ~1.9 MB/min; 30-min meeting ~ 57 MB, 60-min ~ 115 MB
- Key on device-id + filename — REC_NNN.wav is only unique per card. Land files under uploads/recordings/<device-id>/
- Success response: 201 Created with JSON body {"filename": "...", "bytes_written": N} confirming persisted to disk, complete. Device acks only on this. "Bytes received" is not enough — that's a silent data loss bug.
- Failure: 4xx/5xx — device does not ack, retries next cycle. Endpoint must be idempotent — re-POST of same device-id+filename overwrites/dedupes, never duplicates.

mDNS service advertisement (new Headspace-side requirement):
- Headspace must advertise _otl-recordings._tcp via mDNS on port 15055 at app startup using the Python zeroconf library. This is how the device discovers the server — no hardcoded IPs.
- Register in create_app(), unregister on shutdown.

PRD scope — what needs to be built:
1. mDNS service advertisement (_otl-recordings._tcp on port 15055)
2. REST endpoint implementing the above contract — raw WAV body receiver with disk-write integrity verification
3. Pipeline trigger: detect new recording arrival, initiate transcription
4. Deepgram batch transcription API call with speaker diarization enabled
5. Land transcript in data/transcripts-recordings/ in Deepgram's native output format
6. Error handling: Deepgram failures, partial uploads, retries

Key constraints:
- Single-mic mono audio — diarization is best-effort speaker separation from one mixed channel. Accuracy depends on mic placement and room acoustics.
- Transcript format: use Deepgram's native diarized output as-is. No format conversion.
- Speaker labels: decide in this workshop whether numbered speakers (Speaker 1, 2) suffice or named mapping is needed.
- Downstream processing of transcripts (what Headspace does after the file lands) is OUT of scope.

Participants needed: Architect (Robbo), backend developer with Headspace context, anyone with Deepgram batch API experience.
