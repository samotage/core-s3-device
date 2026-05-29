#ifndef __APPPOWER_MODEL_H
#define __APPPOWER_MODEL_H

#include "lvgl.h"
#include "M5Unified.h"
#include "config.h"

namespace Page {

class AppPowerModel {
   public:
    union {
        struct {
            uint8_t bus : 1;
            uint8_t usb : 1;
            uint8_t reserve : 6;
        } mode_bit;
        uint8_t power_mode = 0x00;
    };

   float vbat, ts, vbus, vsys, tdie;

   public:
   void SetPowerMode(uint8_t mode);
   uint8_t GetPowerMode();
   void AxpCHGLedEnable();
   void AxpCHGLedDisable();
   void AxpAdcEnable();;
   void AxpAdcSampling();
   uint8_t AxpBatIsCharging();  // 0: not charging, 1: charge 2: discharg

   // --- Recorder standalone experience (PRD FR2, FR31, FR26, FR22) ----------
   // Convert AXP2101 raw vbat reading (mV) to LiPo percentage via a
   // discharge-curve lookup. Voltage is flat in the middle of a LiPo discharge
   // curve and drops sharply at the ends — a linear mapping would lie.
   static uint8_t BatteryPercentFromMv(uint16_t mv);

   // Sample battery in mV (calls AxpAdcSampling internally) — convenience for
   // callers that just need the voltage. Returns 0 if read fails.
   uint16_t SampleBatteryMv();

   // Critical-battery predicate (FR31): true when either percent <=
   // CRITICAL_BATTERY_PERCENT or mv <= CRITICAL_BATTERY_VOLTAGE_MV. Voltage
   // must be a real reading (>0). Used by the recorder loop to trigger
   // auto-save + shutdown on a low battery.
   bool IsCriticalBattery();

   // AXP2101 hardware shutdown (FR26, FR32). After this call the device is
   // off; only the power-key (AXP2101 hardware) can wake it.
   void PowerOff();

   // Enter ESP32 deep sleep (FR22, FR23). Configures touch + power-key wake
   // sources before calling esp_deep_sleep_start(); call returns only via
   // wake-and-reboot.
   void DeepSleep();

   private:
};

}  // namespace Page

#endif
