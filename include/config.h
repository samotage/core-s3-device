#ifndef __CONFIG_H
#define __CONFIG_H

#define DEMO_VERSION "0.6"

#define GC0308_ADDR  0x21
#define LTR553_ADDR  0x23
#define AXP2101_ADDR 0x34
#define AW88298_ADDR 0x36
#define FT6336_ADDR  0x38
#define ES7210_ADDR  0x40
#define BM8563_ADDR  0x51
#define AW9523_ADDR  0x58
#define BMI270_ADDR  0x69
#define BMM150_ADDR  0x10

#define SYS_I2C_PORT 0
#define SYS_I2C_SDA  12
#define SYS_I2C_SCL  11

#define EXT_I2C_PORT 0

#define PORTA_PIN_0  1
#define PORTA_PIN_1  2
#define PORTB_PIN_0  8
#define PORTB_PIN_1  9
#define PORTC_PIN_0  18
#define PORTC_PIN_1  17

#define POWER_MODE_USB_IN_BUS_IN 0
#define POWER_MODE_USB_IN_BUS_OUT 1
#define POWER_MODE_USB_OUT_BUS_IN 2
#define POWER_MODE_USB_OUT_BUS_OUT 3

#define MIC_BUF_SIZE 256

#define MONKEY_TEST_ENABLE 0

// --- Recorder standalone experience ---------------------------------------
// Screen auto-off timeout (FR19): backlight off after this many ms idle.
#define SCREEN_IDLE_TIMEOUT_MS 60000

// Status bar refresh cadence (NFR2): >= 5s. Picked to not disrupt mic DMA.
#define STATUS_BAR_REFRESH_MS  5000

// Storage-full threshold (FR33): block recording below this many free bytes.
#define STORAGE_FULL_THRESHOLD_BYTES 5000000ULL

// Critical-battery threshold (FR31): trip auto-save + shutdown at this point.
// Both percent and voltage are provided; predicate ORs them so either signal
// (whichever is more reliable on the bench at the time) can fire the trip.
#define CRITICAL_BATTERY_PERCENT 8
#define CRITICAL_BATTERY_VOLTAGE_MV 3300

// Default brightness on boot (Settings page overrides at runtime).
#define DEFAULT_BRIGHTNESS 180

#endif  // __CONFIG_H