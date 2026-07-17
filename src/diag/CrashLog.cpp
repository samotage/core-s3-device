#include "CrashLog.h"

#include <SD.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Shared SPI-bus mutex (created in m5gfx_lvgl_init). Null before that runs, in
// which case there is no other bus user yet and the take/give are no-ops.
extern SemaphoreHandle_t g_spi_bus_mutex;

namespace Diag {

// Arbitrary but distinctive; guards against RTC RAM coming back as garbage after
// a power-off. A random 32-bit collision is ~1 in 4.3e9.
#define CRASH_MAGIC   0xC0FFEE17UL

// THE black-box block. RTC_NOINIT_ATTR keeps it out of .bss so the startup code
// never clears it — that is the whole point.
RTC_NOINIT_ATTR static RecState s_state;

static RecState           s_prev;              // previous boot's block (copied at Begin)
static bool               s_prev_valid = false;
static esp_reset_reason_t s_reason     = ESP_RST_UNKNOWN;
static char               s_summary[240];

static inline void bus_take() { if (g_spi_bus_mutex) xSemaphoreTake(g_spi_bus_mutex, portMAX_DELAY); }
static inline void bus_give() { if (g_spi_bus_mutex) xSemaphoreGive(g_spi_bus_mutex); }

// Reset-reason names. This is the byte that ends the "why did it stop?" question:
//   POWERON  -> rails dropped (AXP2101 shutdown / power loss), NOT a firmware crash
//   PANIC    -> firmware crash (LoadProhibited, assert, etc.)
//   TASK_WDT -> a task starved the watchdog (capture/writer/bus contention)
//   INT_WDT  -> interrupts blocked too long (an ISR or a critical section)
//   BROWNOUT -> supply dipped below the detector threshold
static const char* reason_str(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

void Begin() {
    s_reason = esp_reset_reason();

    s_prev_valid = (s_state.magic == CRASH_MAGIC);
    if (s_prev_valid) {
        s_prev = s_state;   // snapshot before we reuse the block
    } else {
        memset(&s_prev, 0, sizeof(s_prev));
    }

    // Re-arm the block for this boot. boot_count only carries across resets that
    // preserve RTC RAM; a power-off legitimately restarts the count, and that
    // restart is itself a signal worth seeing in the log.
    uint32_t next_boot = s_prev_valid ? s_prev.boot_count + 1 : 1;
    memset(&s_state, 0, sizeof(s_state));
    s_state.magic         = CRASH_MAGIC;
    s_state.boot_count    = next_boot;
    s_state.was_recording = 0;
    s_state.min_free_ever = 0xFFFFFFFFUL;

    if (s_prev_valid && s_prev.was_recording) {
        snprintf(s_summary, sizeof(s_summary),
                 "BOOT #%lu reason=%s | DIED MID-RECORDING %s bytes=%lu secs=%lu "
                 "uptime=%lums heap_int=%lu largest=%lu psram=%lu min_free=%lu",
                 (unsigned long)next_boot, reason_str(s_reason),
                 s_prev.filename[0] ? s_prev.filename : "?",
                 (unsigned long)s_prev.rec_bytes, (unsigned long)s_prev.rec_secs,
                 (unsigned long)s_prev.uptime_ms, (unsigned long)s_prev.free_internal,
                 (unsigned long)s_prev.largest_block, (unsigned long)s_prev.free_psram,
                 (unsigned long)s_prev.min_free_ever);
    } else if (s_prev_valid) {
        snprintf(s_summary, sizeof(s_summary),
                 "BOOT #%lu reason=%s | clean (no capture in progress at last reset)",
                 (unsigned long)next_boot, reason_str(s_reason));
    } else {
        // No valid block. Either the very first boot after a flash, or the RTC
        // domain lost power — which, paired with reason=POWERON, means the device
        // was powered down rather than reset.
        snprintf(s_summary, sizeof(s_summary),
                 "BOOT #%lu reason=%s | no prior state in RTC RAM "
                 "(power-off, or first boot after flash)",
                 (unsigned long)next_boot, reason_str(s_reason));
    }

    Serial.printf("[DIAG] %s\n", s_summary);
    Serial.flush();
}

void FlushToSD() {
    static bool     written    = false;
    static uint32_t last_try_ms = 0;
    if (written) return;

    // Throttle: the loop calls this every ~10 ms, and a card-less boot would
    // otherwise hammer SD.open() on the shared SPI bus forever. A capture cannot
    // start without a mounted card, so this never contends with the writer task.
    uint32_t now = millis();
    if (last_try_ms != 0 && (now - last_try_ms) < 2000) return;
    last_try_ms = now;

    bus_take();
    File f = SD.open(DIAG_LOG_PATH, FILE_APPEND);
    bool ok = (bool)f;
    if (ok) {
        f.println(s_summary);
        f.flush();
        f.close();
    }
    bus_give();

    if (ok) {
        written = true;
        Serial.println("[DIAG] boot record appended to " DIAG_LOG_PATH);
    }
    // Not written -> card isn't mounted yet. Caller may retry; we stay silent so
    // a card-less bench boot doesn't spam the log.
}

void MarkRecordingStart(const char* filename) {
    snprintf(s_state.filename, sizeof(s_state.filename), "%s", filename ? filename : "?");
    s_state.rec_bytes     = 0;
    s_state.rec_secs      = 0;
    s_state.uptime_ms     = millis();
    s_state.min_free_ever = 0xFFFFFFFFUL;
    s_state.was_recording = 1;   // set LAST: the flag is what makes the block meaningful
}

void Tick(uint32_t bytes_written, uint32_t secs) {
    if (!s_state.was_recording) return;
    s_state.rec_bytes     = bytes_written;
    s_state.rec_secs      = secs;
    s_state.uptime_ms     = millis();
    s_state.free_internal = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_state.largest_block = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    s_state.free_psram    = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (s_state.free_internal < s_state.min_free_ever) {
        s_state.min_free_ever = s_state.free_internal;
    }
}

void MarkRecordingStop() {
    s_state.was_recording = 0;   // a clean stop: the next boot must not read this as a death
}

const char* BootSummary() { return s_summary; }

bool DiedWhileRecording() { return s_prev_valid && s_prev.was_recording != 0; }

}  // namespace Diag
