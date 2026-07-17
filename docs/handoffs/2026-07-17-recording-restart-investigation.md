# Recording Restart Investigation — Handoff

**Task:** Headspace #1679 "Recording failure"
**Incident date:** 2026-07-17 (home, Google Meet)
**Investigator:** Chip (firmware)
**Status:** Root cause of the restart NOT yet captured — instrumentation shipped, awaiting the next real-meeting failure to name it.
**Device:** CoreS3, LAN IP `192.168.4.16`, MAC `44:1b:f6:e1:fc:e8`

> Pick-up-cold TL;DR: the recorder did not *stop* — the device *restarted* mid-meeting. We proved that from the artifacts. We could not prove *why* (no reset-reason logging existed at the time). A black box now exists on the device and is armed. When it next cuts out, run `curl http://192.168.4.16/api/diag` and it will name the cause. The bench cannot reproduce it — a real meeting is required.

---

## 1. The one-line conclusion

**Two meeting recordings (REC_032, REC_033) were severed because the device
restarted mid-capture — it did not stop the recording.** REC_034 was a normal
manual stop at the meeting's end, not a failure.

Why it restarted is still unknown: `esp_reset_reason()` was never read and no
state survived the boot, so the cause from 2026-07-17 is gone. The fix for *that*
(a persistent black box) is now flashed and verified; it will name the cause on
the next occurrence.

---

## 2. Evidence that proved "restart, not stop"

All measured from the three WAVs + Headspace's `app.log`, not inferred.

| Observation | How measured | What it rules in/out |
|---|---|---|
| REC_032 = **923.968 s**, REC_033 = **922.624 s** (Δ 1.34 s, 0.15%) | WAV data size ÷ 32000 B/s | Machine-timed, not a human tap. But NOT a fixed ceiling (see below). |
| REC_034 = 676.35 s, last words *"Have a good weekend… See you."* | Deepgram transcript | Natural end — **not a failure**. Manual stop. |
| REC_032/033 cut **mid-sentence**, speech to the literal final sample | transcript last-word end == file end | Hard death, no drain/finalise. Not a mic-death (no silent tail). |
| Every WAV header matches its byte length **exactly** | RIFF/data size vs `stat` | `StopRecording()` never ran. Files survived ONLY on the FR12 5 s header flush. A partial short-write would leave the file longer than its header — it didn't. |
| The photographed screen: copper **"Record"** button, **blank** status line | Sam's photo + `AppRecorderView.cpp` | Only `SetIdle()` via `onViewLoad()`/`ShowIdle()` renders that. A button-stop leaves "✓ Saved REC_0xx.wav"; a fault leaves red text on a red "Stop" button. **Neither present → fresh page load → the device rebooted.** |
| Headspace upload logged at 13:50:43 (one POST per file) | `claude_headspace/logs/app.log` | This is the **boot's** un-acked push (`upload_pending=true` on first WiFi-up, `RecorderServer.cpp:124`), NOT a post-stop push. It is the reboot's fingerprint. |
| Battery 94% in the photo | Sam's photo | Critical-battery auto-save path (`AppRecorder.cpp:173`) cannot have fired — it would also `PowerOff()`. |

### The killer detail
- **The wall resets.** REC_032 died at ~923 s, then REC_033 got a *fresh, full*
  ~923 s before dying again. Whatever accumulates is cleared by
  `StartRecording()`/`StopRecording()`.
- **No ceiling exists in code.** The same card holds **REC_025 = 79.1 min** and
  **REC_028 = 78.4 min**. The device has recorded 79 minutes straight on this
  firmware. So the ~923 s pair is *situational*, not a hard limit.
- **Card-full is out.** 51 MB was written *after* the failures (REC_033 + 034).

---

## 3. What is NOT the cause (ruled out — do not re-raise)

- **SD card** — brand-new good SanDisk; the isolated SD stress tests (T/U/V/W)
  all passed in the May investigation. Not the card.
- **Battery** — brand-new LiPo, healthy; and the device is on **USB power**
  anyway (measured: `charging=0` = not discharging, VBUS carrying the system).
- **Battery-vs-USB power delivery** — CLOSED. Bench soak ran on the same USB
  power config as the meeting (vbat 4134 mV, charging=0). Not the delta.
- **A fixed time-wall / steady leak / basic capture-path defect** — the 20 min
  bench soak (REC_036, 1200.13 s) ran clean straight past 923 s. Not these.
- **The short-write fault path** (the May 2026 cut-off cause) — that path leaves
  a file longer than its header and prints a red error. Neither seen here. This
  is a DIFFERENT bug from the May incident.

---

## 4. What was built (all shipped, flashed, verified on hardware)

### 4a. Black box — `src/diag/CrashLog.{h,cpp}`  (commit `0eab736`)
Persists across a restart so the next boot can explain the last one.
- `RTC_NOINIT_ATTR` block — **verified in the ELF at `0x50000000` in
  `.rtc_noinit`** (not `.bss`, so startup code never zeroes it). Survives
  panic / task-WDT / int-WDT / brownout resets, but NOT a true power-off. That
  asymmetry is the point: a `POWERON` boot with no valid block = rails dropped;
  a valid block = we crashed.
- `esp_reset_reason()` read FIRST in `setup()` (before anything disturbs RTC RAM).
- Mirrors the live capture ~1 Hz: filename, bytes, secs, uptime, heap/PSRAM +
  low-water; and ~5 s: vbat, min_vbat, charging.
- Appends one line per boot to `/DIAG.log` on the card.
- **`was_recording` flag** set on start, cleared on clean stop — so only an
  un-clean death reads back as "DIED MID-RECORDING".

### 4b. `GET /api/diag`  (commit `0eab736`, extended `fe14771`)
Serves boot summary + live power + full `/DIAG.log` over the LAN — no serial
cable. This is the exact gap that left the 2026-07-17 incident unexplained.

### 4c. `POST /api/record?secs=N` — hands-off soak harness  (commit `522c377`)
Fixed-duration capture that auto-stops. Needed because:
- **App `Serial` is on UART0, unreachable over USB** (since `4f148f3` dropped
  `ARDUINO_USB_CDC_ON_BOOT` to fix battery cold-start). Verified: a serial probe
  over `/dev/cu.usbmodem14301` returns 0 bytes. **USB = flashing only, not
  control.** Do NOT re-enable CDC to get serial back — it re-breaks cold-start.
- The **radio is off for the whole capture**, so the device is unreachable
  mid-recording and can't be told to stop. Duration is committed up front.
- Signature is self-evident: a crash reboots → device reappears on the LAN
  EARLY. Return-time vs requested-duration is the test.

### 4d. Per-capture supply logging  (commit `fe14771`)
`min_vbat_mv` low-water across a capture — if a restart is ever a supply dip,
this is the smoking gun. Rides the existing 5 s battery poll (no extra I2C).

**Commits:** `0eab736` → `522c377` → `fe14771` (all pushed to `master`).
**Build:** flash 93.7%, RAM 34.4%. Diagnostic only — no capture-path behaviour
change.

---

## 5. Test results (2026-07-17, on the bench, USB power)

| Test | Requested | Got | Restart? |
|---|---|---|---|
| Smoke | 30 s | REC_035 = 30.08 s, uploaded + transcribed | no |
| **Soak** | **1200 s** | **REC_036 = 1200.13 s** | **no (boot counter unchanged)** |

**The fault does not reproduce at the bench.** It is meeting-specific.

---

## 6. Open leads (for next session — NOT yet investigated, do not treat as causes)

Ranked by how well they fit "situational, ~923 s, resets each recording,
USB-powered, doesn't reproduce on a quiet bench":

1. **Screen-wake taps on the shared SPI bus.** Sam was *tapping the screen to
   check on it* during the meeting. The LCD and SD share the SPI bus (+ the LCD
   reuses MISO/GPIO35 as D/C), serialised through `g_spi_bus_mutex`. A wake +
   LVGL full-redraw from PSRAM draw buffers, colliding with the writer task and
   the mic, is the known three-way pressure point on this board (see the May
   experience-log entry). **This is the leading hypothesis — but it is a
   hypothesis, unproven. `/api/diag` reason=`TASK_WDT` or `INT_WDT` would
   support it.**
2. **RF environment.** Even with the radio OFF during capture, the room was full
   of 2.4 GHz (Meet on WiFi, other devices). Lower prior — radio is off — but
   note it.
3. **Backlight-off + live-touch defect (found, NOT fixed).** `App.cpp:24` blanks
   the backlight during recording after 60 s, but **touch stays live with no
   wake-guard** — a check-tap on a dark screen lands straight on the button
   underneath. Did NOT cause this incident (1.34 s apart ≠ human tap), but it's
   an independent live way to lose a meeting, and it interacts with lead #1.
   Fix offered, awaiting Sam's word.

**Method discipline for next time (hard-won):** reproduce and instrument the
fault ourselves — do NOT theory-volley at Sam, do NOT use him as the test rig.
The black box exists precisely so the *device* reports the cause instead of us
guessing.

---

## 7. Exactly what to do when it next fails

1. **Do not power-cycle the device.** The black box survives a reset but NOT a
   power-off — a power-cycle destroys the evidence.
2. Read it:
   ```
   curl http://192.168.4.16/api/diag
   ```
3. Read the `DIED MID-RECORDING` line. Interpret `reason=`:
   - `PANIC` → firmware crash (stack trace territory; heap fields show state on
     the way down).
   - `TASK_WDT` → a task starved the watchdog — **supports lead #1** (SPI/bus
     contention from a screen-wake).
   - `INT_WDT` → interrupts blocked too long — also bus/critical-section.
   - `BROWNOUT` → supply dipped — check `min_vbat` on the same line.
   - `POWERON` with "no prior state in RTC RAM" → rails actually dropped (power
     loss), not a firmware crash.
4. Note `secs=` (how far it got), `min_free` (heap low-water), `min_vbat` (supply
   low-water). Those localise it.
5. Reproduce hands-off if useful: `curl -X POST "http://192.168.4.16/api/record?secs=1200"`
   — device goes offline while recording, returns early if it crashed.

---

## 8. Key file/line references

- Capture lifecycle: `src/pages/AppRecorder/AppRecorderModel.cpp`
  (`StartRecording` 184, `StopRecording` 222, writer `WriterLoop` 116).
- Stop paths: `src/pages/AppRecorder/AppRecorder.cpp` (fault 133, critical-batt
  173, button 214, remote-auto-stop in `Update`).
- Screen states: `src/pages/AppRecorder/AppRecorderView.cpp` (`SetIdle` 109,
  `SetRecording` 120, `SetSaved` 130, `SetError` 138).
- WiFi-off-during-capture: `src/net/RecorderServer.cpp:75-88` (and the comment
  there — WiFi/I2S contention crashed the device ~13 min in a prior incident;
  uncomfortably close to the ~15.4 min REC_032 duration).
- Backlight-off + live touch: `src/App.cpp:19-35`.
- Black box: `src/diag/CrashLog.{h,cpp}`; endpoints in `src/net/RecorderServer.cpp`
  (`handleDiag`, `/api/record`).

## 9. Flash protocol (this board — hard-won, do not deviate)

- **115200 baud only.** The default 460800 drops serial mid-flash.
- Factory-only `partitions_ffat.csv`: flash **bootloader + partitions + firmware
  only** — NEVER `boot_app0.bin` at `0xe000` (causes a `TG0WDT_SYS_RST` loop).
- esptool via the pipx PlatformIO venv (has pyserial):
  `~/.local/pipx/venvs/platformio/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py`
- After flashing, `--after hard_reset` and confirm it rejoins WiFi (~12 s) — do
  not leave it parked in the bootloader (black screen = "looks dead").
