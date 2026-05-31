#include "AppRecorderModel.h"
#include "M5Unified.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "freertos/semphr.h"

using namespace Page;

// Shared SPI-bus mutex (created in m5gfx_lvgl_init). The CoreS3 LCD and SD card
// share the SPI bus AND the LCD reuses MISO (GPIO35) as its D/C line, so every
// SD op (writer task, core 0) and every LCD flush (core 1) must be serialised
// through this one mutex (PRD FR8-FR10).
extern SemaphoreHandle_t g_spi_bus_mutex;

static inline void bus_take() { if (g_spi_bus_mutex) xSemaphoreTake(g_spi_bus_mutex, portMAX_DELAY); }
static inline void bus_give() { if (g_spi_bus_mutex) xSemaphoreGive(g_spi_bus_mutex); }

// App-level recording-model singleton.
namespace Page {
AppRecorderModel* g_app_recorder_model = nullptr;
}

void AppRecorderModel::MicBegin() {
    M5.Speaker.end();
    M5.Mic.begin();
}

void AppRecorderModel::MicEnd() {
    if (recording) StopRecording();
    M5.Mic.end();
    M5.Speaker.begin();
}

// TF card detect: AW9523B P0.4 reads low when a card is seated.
bool AppRecorderModel::IsSDCardPresent() {
    return !((M5.In_I2C.readRegister8(AW9523_ADDR, 0x00, 100000L) >> 4) & 0x01);
}

bool AppRecorderModel::InitSD() {
    if (sd_ready) return true;
    Serial.println("[REC] mounting SD...");
    Serial.flush();
    // FR17 / K5: format_if_empty=FALSE. NEVER auto-format — on 2026-05-31 the
    // auto-format reformatted and wiped the user's recordings during recovery.
    // On mount failure we surface an error and refuse to record (FR18), leaving
    // the card untouched so its data is recoverable on a computer.
    sd_ready = SD.begin(GPIO_NUM_4, SPI, 25000000, "/sd", 5, /*format_if_empty=*/false);
    if (sd_ready) {
        file_num = FindNextFileNum();
        Serial.printf("[REC] SD ready, next file: REC_%03d.wav  (free: %llu MB)\n",
                      file_num, (unsigned long long)SDFreeMB());
    } else {
        Serial.println("[REC] SD mount FAILED — refusing to record (card NOT formatted)");
    }
    return sd_ready;
}

uint64_t AppRecorderModel::SDFreeMB() {
    if (!sd_ready) return 0;
    uint64_t total = SD.totalBytes();
    uint64_t used  = SD.usedBytes();
    if (total < used) return 0;
    return (total - used) / (1024ULL * 1024ULL);
}

int AppRecorderModel::FindNextFileNum() {
    int n = 1;
    char path[20];
    while (true) {
        snprintf(path, sizeof(path), "/REC_%03d.wav", n);
        if (!SD.exists(path)) return n;
        n++;
    }
}

void AppRecorderModel::SetFault(const char* msg) {
    snprintf(fault_msg, sizeof(fault_msg), "%s", msg);
    fault = true;
}

// --- WAV header (writer task only) ------------------------------------------
void AppRecorderModel::WriteHeader() {
    RecWavHeader header;
    header.data_size = bytes_written;
    header.file_size = bytes_written + sizeof(RecWavHeader) - 8;
    wav_file.seek(0);
    wav_file.write((uint8_t*)&header, sizeof(header));
    wav_file.seek(sizeof(RecWavHeader) + bytes_written);
}

// Open the next REC_NNN.wav and write the zeroed placeholder header. Writer-side,
// under the bus mutex. Returns false (and sets fault) if the card won't open.
bool AppRecorderModel::OpenNewFile() {
    bus_take();
    wav_file = SD.open(last_filename, FILE_WRITE);
    bool ok = (bool)wav_file;
    if (ok) {
        RecWavHeader header;  // zeroed sizes — finalised on stop
        ok = (wav_file.write((uint8_t*)&header, sizeof(header)) == sizeof(header));
    }
    bus_give();
    if (!ok) {
        Serial.printf("[REC] writer: failed to open %s\n", last_filename);
        SetFault("SD open failed");
        return false;
    }
    return true;
}

// --- SD-writer task body (pinned to core 0, PRD §6) -------------------------
void AppRecorderModel::WriterTrampoline(void* arg) {
    static_cast<AppRecorderModel*>(arg)->WriterLoop();
    vTaskDelete(nullptr);
}

void AppRecorderModel::WriterLoop() {
    // 32 KB drain block in internal DMA-capable RAM (SD SPI DMA reads from it).
    uint8_t* block = (uint8_t*)heap_caps_malloc(REC_WRITE_BLOCK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!block) { SetFault("OOM writer block"); writer_done = true; return; }

    if (!OpenNewFile()) { heap_caps_free(block); writer_done = true; return; }
    Serial.printf("[REC] Recording -> %s\n", last_filename);

    uint32_t last_flush = millis();

    while (true) {
        size_t got = xStreamBufferReceive(ring, block, REC_WRITE_BLOCK, pdMS_TO_TICKS(100));
        if (got > 0) {
            bus_take();
            size_t w = wav_file.write(block, got);
            bus_give();
            if (w != got) {
                // FR15 (resolved per §11): a single SD.end()/begin() does NOT clear
                // the observed wedge — it needs a full power cycle (bench-confirmed
                // 2026-05-31). So re-init is dead code; we go straight to stop-with-
                // error. File stays valid up to the last 5 s header rewrite.
                Serial.printf("[REC] SD SHORT WRITE got=%u/%u at %u bytes — STOP\n",
                              (unsigned)w, (unsigned)got, (unsigned)bytes_written);
                Serial.flush();
                SetFault("SD fault - restart device");
                break;
            }
            bytes_written += w;
        }

        uint32_t now = millis();
        if (now - last_flush >= REC_FLUSH_MS) {     // FR12 power-loss-safe header
            bus_take();
            WriteHeader();
            wav_file.flush();
            bus_give();
            last_flush = now;
        }

        if (stop_requested) {                        // FR13 drain-and-finalise handshake
            while ((got = xStreamBufferReceive(ring, block, REC_WRITE_BLOCK, 0)) > 0) {
                bus_take();
                size_t w = wav_file.write(block, got);
                bus_give();
                if (w != got) { SetFault("SD fault - restart device"); break; }
                bytes_written += w;
            }
            break;
        }
    }

    // Finalise: real header on a clean stop; best-effort on a fault (the 5 s
    // periodic rewrite already left the file playable up to ~5 s before the fault).
    bus_take();
    WriteHeader();
    wav_file.flush();
    wav_file.close();
    bus_give();
    Serial.printf("[REC] Saved %s (%u bytes, %us)%s\n", last_filename,
                  (unsigned)bytes_written, (unsigned)(bytes_written / (REC_SAMPLE_RATE * 2)),
                  fault ? "  [FAULT]" : "");
    Serial.flush();

    heap_caps_free(block);
    writer_done = true;
}

// --- lifecycle (core 1) -----------------------------------------------------
bool AppRecorderModel::StartRecording() {
    if (recording) return false;
    fault = false; fault_msg[0] = 0;
    if (!InitSD()) { SetFault("No SD card"); return false; }   // FR18

    snprintf(last_filename, sizeof(last_filename), "/REC_%03d.wav", file_num);

    // FR1: allocate the 1 MB ring in PSRAM. StreamBuffer needs storage+1 bytes.
    ring_storage = (uint8_t*)heap_caps_malloc(REC_RING_BYTES + 1, MALLOC_CAP_SPIRAM);
    if (!ring_storage) { SetFault("PSRAM alloc failed"); return false; }  // FR3
    ring = xStreamBufferCreateStatic(REC_RING_BYTES + 1, /*trigger=*/REC_WRITE_BLOCK,
                                     ring_storage, &ring_struct);
    if (!ring) {
        heap_caps_free(ring_storage); ring_storage = nullptr;
        SetFault("ring create failed"); return false;
    }

    bytes_written  = 0;
    level          = 0;
    mic_fail_streak = 0;
    stop_requested = false;
    writer_done    = false;
    recording      = true;

    // FR4 / D9: writer pinned to core 0 (idle during capture — WiFi is OFF).
    BaseType_t ok = xTaskCreatePinnedToCore(WriterTrampoline, "sd_writer",
                                            /*stack bytes=*/8192, this,
                                            /*prio=*/5, &writer_task, /*core=*/0);
    if (ok != pdPASS) {
        recording = false;
        vStreamBufferDelete(ring); ring = nullptr;
        heap_caps_free(ring_storage); ring_storage = nullptr;
        SetFault("writer task failed");
        return false;
    }
    return true;
}

void AppRecorderModel::StopRecording() {
    if (!recording) return;
    recording      = false;   // capture stops pushing immediately
    stop_requested = true;     // writer drains the remainder, finalises, closes

    // Wait for the writer's drain-and-finalise handshake (FR13). Generous cap —
    // a dead-card writer still returns within the SD driver timeout, not forever.
    uint32_t t0 = millis();
    while (!writer_done && (millis() - t0) < 8000) vTaskDelay(pdMS_TO_TICKS(10));
    if (!writer_done) Serial.println("[REC] WARN: writer did not finish in time");

    vTaskDelay(pdMS_TO_TICKS(20));   // let the trampoline self-delete
    writer_task = nullptr;
    if (ring) { vStreamBufferDelete(ring); ring = nullptr; }       // FR1 free on stop
    if (ring_storage) { heap_caps_free(ring_storage); ring_storage = nullptr; }

    level          = 0;
    stop_requested = false;
    file_num++;
}

// --- capture (core 1, LVGL timer) -------------------------------------------
bool AppRecorderModel::CaptureChunk() {
    if (!recording) return false;

    if (!M5.Mic.record(rec_chunk, REC_CHUNK_SIZE, REC_SAMPLE_RATE)) {
        // mic-fail trigger retained (PRD §2.2) but recovery is a codec reset on
        // THIS thread (the mic owner) — NOT a file rollover (the writer owns the
        // file now). Same file continues after a brief gap. Rate-limited.
        if (++mic_fail_streak >= 30 && (millis() - last_mic_reset_ms) >= 10000) {
            Serial.printf("[REC] mic-fail streak=%u — resetting codec\n", mic_fail_streak);
            M5.Mic.end(); delay(20); M5.Mic.begin();
            mic_fail_streak  = 0;
            last_mic_reset_ms = millis();
        }
        return false;
    }
    mic_fail_streak = 0;

    // FR2: push raw PCM into the ring, NON-blocking (timeout 0). Never blocks,
    // never touches SD or the bus mutex. A short send = ring full = the card has
    // stalled longer than ~30 s of buffer = it's dead → stop-with-error (FR14).
    const size_t want = REC_CHUNK_SIZE * sizeof(int16_t);
    size_t sent = xStreamBufferSend(ring, rec_chunk, want, 0);
    if (sent != want) {
        Serial.printf("[REC] RING OVERRUN (sent %u/%u) — card too slow, STOP\n",
                      (unsigned)sent, (unsigned)want);
        Serial.flush();
        SetFault("SD too slow - stopped");
    }
    return true;
}

uint32_t AppRecorderModel::RecordingSeconds() {
    // From audio actually persisted to SD (FR19) — the ticker reflects real
    // captured seconds and never climbs while bytes aren't reaching the card.
    return bytes_written / (REC_SAMPLE_RATE * 2);
}
