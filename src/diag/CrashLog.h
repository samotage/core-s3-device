#ifndef __DIAG_CRASH_LOG_H
#define __DIAG_CRASH_LOG_H

#include <Arduino.h>

// Black box for the recorder.
//
// Why this exists: on 2026-07-17 two meeting recordings (REC_032, REC_033) were
// severed mid-sentence at 923.97 s and 922.62 s. The WAVs survived only because
// of the FR12 5 s header flush — StopRecording() never ran — and the screen came
// back on the SetIdle() path (blank status line), i.e. a fresh page load. The
// device restarted. Nothing recorded WHY, because esp_reset_reason() was never
// read and nothing persisted across the boot. This module closes that hole.
//
// The mechanism leans on one useful asymmetry:
//   * RTC "no-init" RAM is NOT zeroed by the startup code, so it survives every
//     reset that keeps the rails up — panic, interrupt/task watchdog, brownout
//     reset, esp_restart().
//   * It does NOT survive a true power-off (AXP2101 dropping VDD), where the
//     region powers down and comes back as garbage.
// So a boot whose block is valid tells us we crashed and how far we got; a boot
// reporting ESP_RST_POWERON with no valid block tells us we lost power instead.
// Those are different faults with different fixes, and this distinguishes them.

// Persisted boot/crash log on the SD card. One line per boot, appended.
#define DIAG_LOG_PATH "/DIAG.log"

namespace Diag {

// Recorder state mirrored into RTC no-init RAM ~1 Hz while capturing. Kept
// deliberately small and POD — it is written from the capture path.
struct RecState {
    uint32_t magic;          // CRASH_MAGIC when this block is meaningful
    uint32_t boot_count;
    uint32_t was_recording;  // non-zero while a capture is live (cleared on clean stop)
    char     filename[24];
    uint32_t rec_bytes;      // PCM bytes persisted at the last tick
    uint32_t rec_secs;       // elapsed seconds at the last tick
    uint32_t uptime_ms;      // millis() at the last tick
    uint32_t free_internal;  // heap_caps_get_free_size(MALLOC_CAP_INTERNAL)
    uint32_t largest_block;  // largest free internal block (fragmentation probe)
    uint32_t free_psram;
    uint32_t min_free_ever;  // low-water mark of free_internal across the capture
};

// Read the reset reason and the previous boot's block. Call FIRST in setup(),
// before anything else can scribble on RTC RAM.
void Begin();

// Append this boot's record to /DIAG.log. Safe to call before the card is
// mounted (it no-ops and can be retried); writes at most once per boot.
void FlushToSD();

void MarkRecordingStart(const char* filename);
void Tick(uint32_t bytes_written, uint32_t secs);  // ~1 Hz from the capture path
void MarkRecordingStop();                          // clean stop — clears was_recording

// One-line summary of THIS boot ("BOOT #12 reason=PANIC | DIED MID-RECORDING ...").
const char* BootSummary();

// True when the previous boot ended with a capture still live — i.e. the device
// died mid-meeting rather than being stopped.
bool DiedWhileRecording();

}  // namespace Diag

#endif
