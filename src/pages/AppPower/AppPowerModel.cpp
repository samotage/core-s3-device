#include "AppPowerModel.h"
#include "recorder_math.h"
#include <esp_sleep.h>

using namespace Page;

uint8_t AppPowerModel::BatteryPercentFromMv(uint16_t mv) {
    return RecorderMath::BatteryPercentFromMv(mv);
}

// P0.2  BUS       LOW[IN]   HIGH[OUT]
// P0.5  USB       LOW[IN]   HIGH[OUT]
// P1.7  BOOST_EN  LOW[OFF]  HIGH[ON]

void AppPowerModel::SetPowerMode(uint8_t mode) {
    switch (mode) {
        case POWER_MODE_USB_IN_BUS_IN:
            M5.In_I2C.bitOff(AW9523_ADDR, 0x02, 0b100010, 100000L);
            M5.In_I2C.bitOff(AW9523_ADDR, 0x03, 0b10000000, 100000L); // BOOST_DIS
            break;
        case POWER_MODE_USB_IN_BUS_OUT:
            M5.In_I2C.bitOff(AW9523_ADDR, 0x02, 0b100000, 100000L);
            M5.In_I2C.bitOn(AW9523_ADDR, 0x02, 0b000010, 100000L);
            M5.In_I2C.bitOn(AW9523_ADDR, 0x03, 0b10000000, 100000L);  // BOOST_EN
            break;
        case POWER_MODE_USB_OUT_BUS_IN:
            M5.In_I2C.bitOn(AW9523_ADDR, 0x02, 0b100000, 100000L);
            M5.In_I2C.bitOff(AW9523_ADDR, 0x02, 0b000010, 100000L);
            M5.In_I2C.bitOff(AW9523_ADDR, 0x03, 0b10000000,
                             100000L);  // BOOST_DIS
            break;
        case POWER_MODE_USB_OUT_BUS_OUT:
            M5.In_I2C.bitOn(AW9523_ADDR, 0x02, 0b100010, 100000L);
            M5.In_I2C.bitOn(AW9523_ADDR, 0x03, 0b10000000, 100000L);  // BOOST_EN
            break;
        default:
            break;
    }

    GetPowerMode();
}

uint8_t AppPowerModel::GetPowerMode() {
    uint8_t data = 0;
    M5.In_I2C.readRegister(AW9523_ADDR, 0x02, &data, 1, 100000L);
    mode_bit.bus = ((data >> 1) & 0b1);
    mode_bit.usb = ((data >> 5) & 0b1);
    // Serial.printf("power mode data raw: %d  bus: %d  usb: %d  mode: %d\r\n",
    //               data, mode_bit.bus, mode_bit.usb, power_mode);
    return power_mode;
}

void AppPowerModel::AxpCHGLedEnable() {
    M5.In_I2C.writeRegister8(AXP2101_ADDR, 0x69, 0b00110101, 100000L);
}

void AppPowerModel::AxpCHGLedDisable() {
    M5.In_I2C.bitOff(AXP2101_ADDR, 0x69, 0b01, 100000L);
}

void AppPowerModel::AxpAdcEnable() {
    M5.In_I2C.writeRegister8(AXP2101_ADDR, 0x30, 0b111111, 100000L);
}

void AppPowerModel::AxpAdcSampling() {
    uint8_t reg     = 0x34;
    uint8_t data[2] = {0};
    M5.In_I2C.readRegister(AXP2101_ADDR, reg, data, 2, 100000L);
    vbat = ((data[0] & 0x3f) << 8) | data[1];
    reg += 2;
    M5.In_I2C.readRegister(AXP2101_ADDR, reg, data, 2, 100000L);
    ts = (((data[0] & 0x3f) << 8) | data[1]) * 0.5f;
    reg += 2;
    M5.In_I2C.readRegister(AXP2101_ADDR, reg, data, 2, 100000L);
    vbus = ((data[0] & 0x3f) << 8) | data[1];
    reg += 2;
    M5.In_I2C.readRegister(AXP2101_ADDR, reg, data, 2, 100000L);
    vsys = (((data[0] & 0x3f) << 8) | data[1]);

    reg += 2;
    M5.In_I2C.readRegister(AXP2101_ADDR, reg, data, 2, 100000L);
    tdie = 22 + ((7274 - ((data[0] << 8) | data[1])) / 20);

    data[0] = M5.In_I2C.readRegister8(AXP2101_ADDR, 0x00, 100000L);

    if (!(data[0] & 0b100000)) {
        vbus = 0.0f;
    }

    if (vbus >= 16375) {
        vbus = 0.0f;
    }
}

uint8_t AppPowerModel::AxpBatIsCharging() {
    uint8_t reg     = 0x01;
    uint8_t data[1] = {0};
    M5.In_I2C.readRegister(AXP2101_ADDR, reg, data, 1, 100000L);
    return (data[0] >> 5) & 0b11;
}

uint16_t AppPowerModel::SampleBatteryMv() {
    AxpAdcSampling();
    // vbat from the AXP2101 register is already in mV (LSB = 1 mV).
    if (vbat < 0 || vbat > 5000) return 0;
    return (uint16_t)vbat;
}

bool AppPowerModel::IsCriticalBattery() {
    // Skip when on charger — we never want to "auto-save and shutdown" while
    // the device is plugged in and ramping up.
    uint8_t chg = AxpBatIsCharging();
    if (chg == 1) return false;  // 1 = charging

    uint16_t mv = SampleBatteryMv();
    return RecorderMath::IsCriticalBattery(mv);
}

void AppPowerModel::ConfigurePowerKey() {
    // AXP2101 reg 0x27 = IRQLEVEL[5:4] / OFFLEVEL[3:2] / ONLEVEL[1:0].
    // Source: AXP2101 datasheet reg 0x27 (X-Powers, "IRQLEVEL/OFFLEVEL/ONLEVEL").
    //   ONLEVEL  (power-ON  press time) : 00=128ms 01=512ms 10=1s 11=2s
    //   OFFLEVEL (power-OFF hold  time) : 00=4s    01=6s    10=8s 11=10s
    //   IRQLEVEL (PWROK/IRQ)            : 00=1s    01=1.5s  10=2s 11=2.5s
    //
    // FR-PWRON: set ONLEVEL=128ms so a quick tap of the power key boots the
    // device from a full AXP power-off (PowerOff() drops the rails; the AXP stays
    // alive on battery and powers the system back up on a PWRON press >= ONLEVEL).
    // M5.begin() already writes 0x27=0x00, but we re-assert it explicitly here so
    // a M5Unified version bump can never silently lengthen the power-on press and
    // resurrect the "had to hold ~2s to boot" defect.
    //
    // OFFLEVEL/IRQLEVEL left at the M5 default (4s / 1s). Trade-off: 128ms is
    // intentionally short for a true "quick tap"; if accidental power-ons in
    // transit are ever observed, raise ONLEVEL to 0b01 (512ms) — a one-nibble
    // change to kPowerKeyCfg.
    const uint8_t kPowerKeyCfg = 0x00;  // ONLEVEL=128ms, OFFLEVEL=4s, IRQLEVEL=1s
    uint8_t before = M5.In_I2C.readRegister8(AXP2101_ADDR, 0x27, 100000L);
    M5.In_I2C.writeRegister8(AXP2101_ADDR, 0x27, kPowerKeyCfg, 100000L);
    uint8_t after = M5.In_I2C.readRegister8(AXP2101_ADDR, 0x27, 100000L);
    static const char* kOnLevel[] = {"128ms", "512ms", "1s", "2s"};
    Serial.printf("[PWR] power-key reg 0x27: before=0x%02X after=0x%02X  "
                  "ONLEVEL=%s (tap-to-boot)\n",
                  before, after, kOnLevel[after & 0x03]);
    Serial.flush();
}

uint8_t AppPowerModel::ReadPowerKeyReg() {
    return M5.In_I2C.readRegister8(AXP2101_ADDR, 0x27, 100000L);
}

void AppPowerModel::PowerOff() {
    // AXP2101 register 0x10 bit 0 = power-off command.
    // Writing the bit triggers hardware shutdown immediately.
    M5.In_I2C.bitOn(AXP2101_ADDR, 0x10, 0b1, 100000L);
    // The line above does not return on real hardware; loop just in case.
    while (true) { delay(1000); }
}

void AppPowerModel::DeepSleep() {
    // Wake on AXP2101 IRQ (power-key press) — wired through to a GPIO.
    // The CoreS3 routes the AXP IRQ to GPIO0 (level-low). ext0 supports
    // single-pin level wakeup.
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    // Touch controller (FT6336U) interrupt wake — pin needs hardware verify
    // before enabling on production; left out of ext1 mask for now and
    // recovered via the power key, which is reliable.
    esp_deep_sleep_start();
}
