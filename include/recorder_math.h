#ifndef __RECORDER_MATH_H
#define __RECORDER_MATH_H

// Pure-math helpers for the recorder standalone experience. Header-only so
// they can be linked into both firmware and native unit-test binaries.

#include <stdint.h>
#include <stddef.h>
#include "config.h"

namespace RecorderMath {

// LiPo discharge-curve lookup (FR2). Anchor points chosen for a typical 1S
// LiPo at room temp; voltage is flat in the middle, drops sharply at the ends.
struct LipoAnchor { uint16_t mv; uint8_t pct; };
static const LipoAnchor kLipoCurve[] = {
    {3000,   0},
    {3300,   5},   // critical
    {3500,  10},
    {3700,  30},
    {3800,  50},
    {3900,  70},
    {4050,  85},
    {4200, 100},
};
static const size_t kLipoCurveLen = sizeof(kLipoCurve) / sizeof(kLipoCurve[0]);

inline uint8_t BatteryPercentFromMv(uint16_t mv) {
    if (mv == 0) return 0;
    if (mv <= kLipoCurve[0].mv) return kLipoCurve[0].pct;
    if (mv >= kLipoCurve[kLipoCurveLen - 1].mv) return kLipoCurve[kLipoCurveLen - 1].pct;
    for (size_t i = 1; i < kLipoCurveLen; ++i) {
        if (mv <= kLipoCurve[i].mv) {
            const LipoAnchor& lo = kLipoCurve[i - 1];
            const LipoAnchor& hi = kLipoCurve[i];
            uint32_t span_mv  = hi.mv - lo.mv;
            uint32_t into_mv  = mv - lo.mv;
            uint32_t span_pct = hi.pct - lo.pct;
            return (uint8_t)(lo.pct + (into_mv * span_pct) / span_mv);
        }
    }
    return 100;
}

// FR15: 16 kHz mono 16-bit = 32000 bytes/sec ≈ 115 MB/hour.
static const uint64_t kRecBytesPerSecond = 32000ULL;
inline uint32_t EstimatedMinutes(uint64_t free_bytes) {
    return (uint32_t)(free_bytes / kRecBytesPerSecond / 60ULL);
}

// FR33: predicate for storage-full.
inline bool IsStorageFull(uint64_t free_bytes) {
    return free_bytes < STORAGE_FULL_THRESHOLD_BYTES;
}

// FR31: critical-battery predicate. Either signal trips it.
inline bool IsCriticalBattery(uint16_t mv) {
    if (mv == 0) return false;  // bad read — don't trigger on noise
    if (mv <= CRITICAL_BATTERY_VOLTAGE_MV) return true;
    return BatteryPercentFromMv(mv) <= CRITICAL_BATTERY_PERCENT;
}

// "Oldest N by filename sort" picker (FR16). Pure: takes a sorted list of
// filenames, returns how many of the first N are valid to delete given the
// total count.
inline int OldestN(int count, int n) {
    if (n <= 0 || count <= 0) return 0;
    return (count < n) ? count : n;
}

}  // namespace RecorderMath

#endif
