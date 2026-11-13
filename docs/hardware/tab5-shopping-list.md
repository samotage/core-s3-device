# M5Stack Tab5 — Kitchen Agent Terminal: Cart Add-ons

Task #3936. Source: M5Stack Tab5 docs (docs.m5stack.com/en/core/Tab5). The shop page rate-limited us (HTTP 429), so the "frequently bought together" list was not checked.

## First, check which SKU is in the cart

| SKU | Contents |
|---|---|
| **K145 Tab5 Kit** | Tab5 + 1× NP-F550 battery (7.4 V, 2000 mAh) + RS485 cable |
| **C145 Tab5** | Tab5 + RS485 cable. **No battery** |

The URL says "kit", but confirm that the cart line reads **K145**.

## Buy

| # | Item | Why |
|---|---|---|
| 1 | **USB-C power supply, 5 V / 3 A or better, plus a 2 m USB-C cable** (a right-angle plug is neater on a bench) | A kitchen terminal runs on mains full time. The battery only charges while the device is **powered on and initialised**. The docs don't give the USB-C PD input spec, so a plain 5 V/3 A supply is the safe choice. |
| 2 | **Mount for the 1/4"-20 tripod thread** (side nut): a mini tabletop tripod, or a 1/4" magnetic or adhesive wall mount for the fridge or splashback | This is the only mounting point. There is no VESA mount or kickstand. |
| 3 | **microSD card** (16–32 GB, a known brand) | Local storage for logs, audio buffers and assets. |

## Optional, later

- **HY2.0-4P (Port A) sensor units**, e.g. an ENV temp/humidity/pressure unit, so the kitchen terminal can also report room conditions. Port A is the only 4-pin port. There is no Port B or C.
- **Small powered speaker on the 3.5 mm jack** if the built-in 1 W speaker is too quiet for agent voice replies over kitchen noise. Try the onboard speaker first.
- **2.4 GHz external antenna**, only if Wi-Fi turns out weak where it's mounted. The docs disagree on the connector (MMCX in one place, SMA in another), so check the connector on the unit before buying one.

## Don't buy yet

- **Zigbee/Thread hub or dongle.** The ESP32-C6 co-processor has an 802.15.4 radio. The assumption, still to be confirmed against the firmware, is that the stock C6 firmware is the Wi-Fi bridge for the P4, so Zigbee/Thread would mean reflashing the C6. Decide on the home-automation approach before spending on it.
- **Cat-M / NB-IoT / LoRa stamp modules.** Not needed for a terminal on house Wi-Fi.

## Notes for the build

- Operating temperature is **0–40 °C**. Keep it away from the cooktop and oven vent.
- **Display IC changed twice** (ILI9881C → ST7123 on 2025-10-14 → ST7121 on 2026-04-28). Check the sticker on the back, and build with **M5Unified ≥ 0.2.23 and M5GFX ≥ 0.2.30**. Older examples may show a blank screen.
- Power it off before pulling the battery. After a hard power loss, wait 5 s before powering on, or the BMI270 IMU may fail to initialise.
- The voice front end is good for an agent terminal: dual mics, ES7210 AEC and an ES8388 codec.
