# RUNBOOK — CoreS3 "dead / black screen / won't boot" recovery

**When to use this:** the device shows a black screen and won't start — power button
does nothing, no logo, appears dead. This has happened at least twice (2026-06 and
2026-08). **It is almost always recoverable in a few minutes. Do not RMA it, do not
replace anything.** Work top to bottom; stop as soon as it boots.

> **The one-line cause:** turning the device **off in software** leaves the AXP2101
> power chip with its `PWROK` line ungated, so the ESP32's reset (`EN`) is never
> released and the app never runs — even though the chip is electrically alive.
> An `esptool` reset toggles `EN` directly and boots it. Full mechanism:
> `docs/handoffs/2026-06-01_power-on-from-battery_PWROK-EN-chain.md`.

---

## DO / DON'T (read first — these are hard-won)

- ✅ **Trust that the chip is alive** if `esptool` can talk to it. A talkable CPU
  means the power system is delivering power — it is **not** a dead power chip,
  **not** an RMA.
- ❌ **Do NOT blame the battery, SD card, or USB cable.** All three are ruled out,
  repeatedly. Suggesting them wastes time and (rightly) infuriates the operator.
  In the 2026-08 incident the battery read **96%** the whole time.
- ❌ **Do NOT `erase_flash`.** It is unnecessary and briefly makes the device look
  like a `TG0WDT_SYS_RST` boot loop. Skip it.
- ❌ **Do NOT flash `boot_app0.bin` at `0xe000`** — that genuinely causes a
  `TG0WDT_SYS_RST` loop. Flash bootloader + partitions + firmware ONLY.
- ❌ **Do NOT use 460800 baud** — it drops mid-flash. **115200 only.**
- ❌ **Do NOT tell the operator to use the software power-off** — that is the very
  thing that bricks it. Screen-sleep or leave on charger instead.

---

## Setup (paste once)

```bash
PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1); echo "PORT=$PORT"   # e.g. /dev/cu.usbmodem14301
PY=~/.local/pipx/venvs/platformio/bin/python
ESPTOOL=~/.platformio/packages/tool-esptoolpy/esptool.py
cd ~/dev/otagelabs/core-s3
```
If `PORT` is empty: the USB cable isn't seated / not a data cable, or the device is
truly off. Ask the operator to plug USB into the device and the Mac, then re-run.

---

## STEP 1 — Confirm the chip is alive (10 s)

```bash
$PY $ESPTOOL --chip esp32s3 --port "$PORT" --baud 115200 --before default_reset --after no_reset flash_id
```
- **Connects (prints `Chip is ESP32-S3`, MAC `44:1b:f6:e1:fc:e8`)** → chip is ALIVE.
  Continue to Step 2. This is the good case and it is the usual one.
- **`Device not configured` / port drops** → the port re-enumerates on reset; just
  retry the command 2–3×. Intermittent connect still means the chip is alive.
- **Never connects at all, port truly absent** → cable/port issue on the host side;
  try another cable/port. Only if it *still* never enumerates is hardware suspect.

## STEP 2 — Boot the app by toggling EN (the fix, ~90% of the time)

No reflash needed — just toggle the reset line and let it boot:
```bash
$PY $ESPTOOL --chip esp32s3 --port "$PORT" --baud 115200 --before default_reset --after hard_reset chip_id
```
Wait ~12 s, then go to **Step 4 (Verify)**. If it comes up, **you're done** — no
reflash required.

## STEP 3 — Only if Step 2 didn't boot it: clean reflash (keep-mode)

Build first if there's no `.pio/build/m5stack-cores3/firmware.bin`:
```bash
~/.local/bin/pio run -e m5stack-cores3
```
Flash bootloader + partitions + firmware ONLY (never boot_app0), keep-mode, and let
the `--after hard_reset` boot it:
```bash
$PY $ESPTOOL --chip esp32s3 --port "$PORT" --baud 115200 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode keep --flash_freq keep --flash_size keep \
  0x0     .pio/build/m5stack-cores3/bootloader.bin \
  0x8000  .pio/build/m5stack-cores3/partitions.bin \
  0x10000 .pio/build/m5stack-cores3/firmware.bin
```
Offsets come from `partitions_ffat.csv`: nvs@0x9000, otadata@0xe000, **factory
app@0x10000**. Every write should print `Hash of data verified`. Then Step 4.

## STEP 4 — Verify it's actually running

```bash
ping -c 3 192.168.4.16                       # on WiFi?
dns-sd -G v4 core-s3.local                   # mDNS resolves? (Ctrl-C after a line)
curl -s http://192.168.4.16/api/diag | head  # web server + SD + battery
```
- `/api/diag` responding = **the app is running** (not just the chip). Look for a
  sane `vbat`/`pct` in the live-power block.
- Ask the operator: **does the screen show the recorder (logo + copper "Record")?**
- Final proof: operator presses **Record → speak → Stop**. The WAV lands in
  `~/dev/otagelabs/claude_headspace/uploads/recordings/core-s3/REC_0NN.wav` and
  transcribes (grep `logs/app.log` for `upload complete` / `transcription complete`).

That is the full definition of done: boots, records, uploads.

---

## Diagnostic — read the boot reset reason (optional, if Step 2/3 loop)

Boot serial is on the USB-JTAG and the port re-enumerates fast, but you can catch the
ROM header. Open the port and read while the device resets (or press the **bottom
RST** button once):
```python
# ~/.local/pipx/venvs/platformio/bin/python  (pyserial dtr=True, rts=False)
import serial, time
p = serial.Serial(PORT, 115200, timeout=0.2)
t=time.time()
while time.time()-t < 20:
    d = p.read(2048)
    if d: print(d.decode('latin1'), end='')
```
Interpreting `rst:` in the ROM line:
- `TG0WDT_SYS_RST` — watchdog reset; usually the `erase_flash`/otadata trap above,
  **not** a hardware fault. Reflash clean (Step 3), don't erase.
- `boot:0x28 (SPI_FAST_FLASH_BOOT)` — normal, it's trying to boot from flash (good).

---

## Root cause & the permanent fix (STILL OPEN)

The device reset (`EN`/`CHIP_PU`) is driven by AXP2101 `PWROK` (net `AXP_PG`). After a
**software** power-off, `PWROK` doesn't re-assert on the next button press, so `EN`
stays held and the app never starts. Stock firmware never does a software power-off.

The lasting fix (do when the device isn't needed same-day, build + verify from
`master`):
- Configure AXP2101 **reg 0x25** `pwrok_chk_en` and/or **reg 0x10 bit3**
  `pwrok_restart_enable` in `AppPowerModel::ConfigurePowerKey()` (called from
  `src/main.cpp setup()`), **or** stop doing a software `PowerOff()` at all.
- Datasheet-ground every bit; log before/after registers (see the `p`/`b` serial
  diagnostics in `AppRecorder.cpp::Update()`).
- Full analysis + schematic references:
  `docs/handoffs/2026-06-01_power-on-from-battery_PWROK-EN-chain.md`.

Until then: **do not use the software power-off** — screen-sleep or charger only.

---

## Incident log

| Date | Trigger | Symptom | Fix that worked |
|---|---|---|---|
| 2026-06-01 | software power-off | powered-but-dark, boots only on esptool reset | EN toggle via `esptool --before default_reset` |
| 2026-08-05 | software power-off (after a meeting) | black, dead to buttons, `TG0WDT` after an `erase_flash` detour | keep-mode reflash + `--after hard_reset` (Step 3); booted, WiFi up, REC_042 recorded + uploaded |
