# Handoff — CoreS3 power-on-from-battery (PWROK→EN reset chain)

**Date:** 2026-06-01
**Repo:** `/Users/samotage/dev/otagelabs/core-s3` (`github.com/samotage/core-s3-device`), branch `master`
**HEAD at handoff:** `bc188ed` (clean tree, all work pushed)
**Device:** M5Stack CoreS3 (ESP32-S3) on `/dev/cu.usbmodem114301`, WiFi `192.168.4.14` / `core-s3.local`

---

## THE OBJECTIVE (do not lose sight of this)

**The physical POWER button must turn the device on.** A short press, on battery,
must boot the device to the recorder screen. This is the ONE remaining defect.
Everything else below is context. If you finish and the button still doesn't power
the device on from battery, the job is not done.

---

## RULED OUT — do NOT re-raise these (operator confirmed, repeatedly)

- **The battery is NOT the problem.** Brand-new LiPo, measures 4.16 V healthy via the
  AXP2101 ADC. Never suggest swapping it, never theorise about cell inrush. See
  `~/.claude/.../memory/battery-is-not-the-problem.md`.
- **The SD card is NOT the problem.** Brand-new SanDisk. See
  `memory/sd-card-is-not-the-problem.md`.
- Do not propose "replug USB", "swap the cable", or any generic remedy as a *fix*.
  The USB-C cable is a known-good Apple cable.

---

## WHAT IS DONE AND PROVEN (do not re-litigate)

- **SD-write remediation: COMPLETE, hardware-verified.** 3× 30-min continuous
  recordings, zero short-writes/wedges/dropped samples, PSRAM flat (no leak). PSRAM
  ring + core-0 SD-writer task + SPI-bus mutex. This was the original mission.
- **Display "looks dead" fixes: COMPLETE.** Two stacked illusions fixed — default
  brightness 80→180, and the 60 s backlight auto-off now only fires *while recording*
  (idle screen stays lit). Operator confirmed on screen.
- **Boot splash: COMPLETE.** Branded "otageLabs / Starting…" frame drawn directly via
  M5GFX in `setup()` before LVGL/SD/WiFi init.
- **Quick-tap power-on ON USB: works.** AXP2101 reg 0x27 ONLEVEL pinned to 128 ms.
- **Stock-vs-fork audit: done.** Every deviation from `m5stack/CoreS3-UserDemo` is
  intentional and accounted for; no latent breakage. Power path now matches stock
  (an earlier speculative BATFET 0x12 write was reverted).

---

## THE REMAINING DEFECT — symptoms (all reproduced on the bench)

1. On battery (USB unplugged), after a power-off, pressing the power button (short OR
   long) does **nothing** — no backlight, no boot. Device appears dead.
2. Plugging USB back in: the chip becomes **electrically alive** (esptool connects,
   reads chip ID `ESP32-S3 rev v0.2`, runs its stub) — BUT the **application firmware
   does not run**: zero serial output, screen dark.
3. The app only starts cleanly after an **esptool reset** (`--before default_reset`,
   which toggles the EN/boot strapping lines directly). A plain USB-power insert does
   NOT reliably cold-start the app.

Net: "powered but dark; boots only when something toggles the reset line."

---

## ROOT-CAUSE MECHANISM (read off the CoreS3 schematic — this is the key finding)

Schematic: `Sch_M5_CoreS3_v1.0.pdf` (from https://docs.m5stack.com/en/core/CoreS3).
Rendered to PNG and read directly. The power-on reset chain is:

```
POWER button (S2) ──► AXP2101 PWRON  (pin 30, net PWR_KEY)   [via R39 510Ω + 1nF + TVS]
AXP2101 PWROK out  (pin 31, net AXP_PG) ──► ESP32-S3 CHIP_PU/EN (pin 4)
```

**The ESP32's reset (EN/CHIP_PU) is driven by the AXP2101's PWROK output (net `AXP_PG`).**
PWROK only asserts — releasing the ESP32 from reset so the app runs — after the AXP
completes a clean power-on sequence with all rails good.

This explains EVERY symptom: on a button/USB power-on the AXP brings up its rails (chip
electrically alive, ROM reachable by esptool) but if **PWROK does not assert cleanly,
EN is never released and the application never starts** → powered-but-dark. An esptool
reset toggles EN directly, bypassing PWROK, which is why only that boots the app. A USB
VBUS insert sometimes forces a fresh PWROK cycle, which is why USB sometimes revives it.

This is the FIRST explanation that fits all three symptoms. It is a **strong hypothesis
with a concrete mechanism — NOT yet proven on hardware.** Treat it as the lead to verify
and fix, not as established fact.

---

## THE FIRMWARE LEVER TO INVESTIGATE

PWROK behaviour is governed by AXP2101 registers we have NOT configured:
- **Reg 0x25 (PowerTimingControl):** bit4 `pwrok_chk_en` ("Check the PWROK Pin enable
  after all dcdc/ldo output valid 128 ms"), bit3 `pwroff_dly_en`, bit2 `pwroff_seq_ctrl`,
  bits1:0 `pwrok_dly`. **`pwrok_chk_en` is currently NOT set** — neither our firmware
  nor `M5.begin()` writes 0x25.
- **Reg 0x10 (CommonConfig) bit3 `pwrok_restart_enable`** ("PWROK PIN pull low to restart
  the system"). We read `0x10=0x30`; bit3 (0x08) is NOT set.

Current measured power-register state on USB (via the `b` serial diagnostic):
```
vbat=4156mV vbus=5076mV vsys=4376mV chg=0
st0(0x00)=0x28 st1(0x01)=0x14 BATFET(0x12)=0x08* cc(0x10)=0x30 chg(0x18)=0x0A
PWRON_src(0x20)=0x01/0x04 PWROFF_src(0x21)=0x01 PWROFF_cfg(0x22)=0x06
Vmin(0x14)=0x65 Ilim(0x16)=0x04
```
*0x12=0x08 is a *latched leftover* from a since-reverted experiment; it clears on a true
battery power-cycle. The firmware no longer writes 0x12 (matches stock).

---

## NEXT STEPS (in order)

1. **VERIFY THE MECHANISM BEFORE CHANGING CODE (reproduce-before-fix).** Confirm the
   PWROK→EN hypothesis with observation, not inference. Options:
   - Read AXP reg 0x25 / 0x10 current values and confirm PWROK-check is disabled.
   - If a scope/logic-analyser is available: probe net `AXP_PG` (AXP pin 31 / ESP32 EN)
     during a battery button-press — does PWROK/EN actually fail to assert? That is the
     direct proof.
   - Cross-check against a KNOWN-GOOD CoreS3 unit if one exists: does stock firmware on a
     healthy unit power on from battery? (Stock `AppPowerModel.cpp` has NO PowerOff/
     DeepSleep at all — stock is only ever powered off by the 6 s hardware button hold.
     **Consider: does our software `PowerOff()` leave the AXP in a state a healthy unit
     never enters?** This is a live possibility — stock never does a software power-off.)

2. **IMPLEMENT the fix** in `src/pages/AppPower/AppPowerModel.cpp::ConfigurePowerKey()`
   (called from `src/main.cpp setup()` after `M5.begin()`):
   - Candidate: set reg 0x25 `pwrok_chk_en` and/or reg 0x10 bit3 `pwrok_restart_enable`
     per the AXP2101 datasheet, so PWROK asserts and releases EN on a clean power-on.
   - Datasheet-ground every bit. Log before/after register values over serial (the
     existing `p` and `b` diagnostics are the pattern to follow).
   - **Strongly consider the simpler hypothesis first:** that the defect is our software
     `PowerOff()` itself putting the AXP into a non-restartable state. If so, the fix may
     be in how/whether we power off, not a new register. Test power-on-from-battery after
     a *hardware* 6 s button-hold power-off (stock's only method) vs after our software
     `PowerOff()` — if the button works after a hardware-hold off but not after software
     off, that isolates it cleanly.

3. **FLASH** (this board is fragile about flashing — follow exactly):
   - esptool at **115200 baud only** (460800 drops mid-flash).
   - Factory-only partitions: write **bootloader + partitions + firmware ONLY**, NEVER
     `boot_app0.bin` at 0xe000 (causes a TG0WDT_SYS_RST boot loop).
   - Exact working command:
     ```
     ~/.local/pipx/venvs/platformio/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
       --chip esp32s3 --port /dev/cu.usbmodem114301 --baud 115200 \
       --before default_reset --after hard_reset \
       write_flash --flash_mode keep --flash_freq keep --flash_size keep \
       0x0 .pio/build/m5stack-cores3/bootloader.bin \
       0x8000 .pio/build/m5stack-cores3/partitions.bin \
       0x10000 .pio/build/m5stack-cores3/firmware.bin
     ```
   - Build: `~/.local/pipx/venvs/platformio/bin/python -m platformio run -e m5stack-cores3`

4. **VERIFY** — the ONLY acceptance test that counts: unplug USB, power the device off,
   press the power button → it boots to the recorder screen. **This requires the operator
   to press a physical button; it CANNOT be verified over serial** (unplugging USB kills
   the serial link). State this plainly; do not claim hardware-verified without the
   operator's physical confirmation. Get the device to a known-running state and ask for
   exactly one physical test.

---

## HARD-WON OPERATIONAL NOTES

- **Boot serial is unreliable to capture** on the CoreS3 USB-Serial/JTAG — the port
  re-enumerates faster than pyserial can reattach, so boot prints fly past. Use the
  on-demand `p` (power-key reg) and `b` (battery/power dump) serial commands instead —
  they work any time the recorder page is active. pyserial: `dtr=True, rts=False`.
  Python at `~/.local/pipx/venvs/platformio/bin/python`.
- Diagnostic scripts on disk: `/tmp/read_bat2.py` (dump power regs), `/tmp/sd_check.py`
  (idle read), `/tmp/ping.py` (send `p`). The `b`/`p` serial handlers live in
  `src/pages/AppRecorder/AppRecorder.cpp::Update()`.
- Recovery from a dark/wedged state: an esptool `--before default_reset` (any command,
  e.g. `chip_id`) cleanly reboots the app. A 6 s+ button hold + USB replug also works.
- Schematic PNGs were rendered to `/tmp/cores3_sch-*.png` and `/tmp/p1_hi-1.png` /
  `/tmp/p3_hi-3.png` (re-render from the PDF with `pdftoppm -r 400` if gone). Page 1 =
  AXP2101 + power button; page 3 = ESP32-S3 (CHIP_PU pin 4 = net AXP_PG).

---

## DISCIPLINE (this session's failures — do not repeat)

- **Do not flash speculative register changes at the device and hope.** Reproduce/observe
  the fault, confirm the mechanism, THEN change code. This session burned the operator's
  whole evening cycling unverified guesses.
- **Do not voice a cause you have not measured.** "It's the battery / the cable / the
  cell" with no observation behind it is the exact anti-pattern that wasted the night.
- **Take command of the bench procedure** — one known state, one variable at a time. Do
  not run a reactive message-by-message firefight at the operator's tempo.
- **Turning the device on is the objective.** Keep it front and centre.
