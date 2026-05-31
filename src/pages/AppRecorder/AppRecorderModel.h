#ifndef __APPRECORDER_MODEL_H
#define __APPRECORDER_MODEL_H

#include "lvgl.h"
#include "config.h"
#include <SD.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

// Meeting-recorder capture format: 16 kHz, mono, 16-bit PCM.
// Whisper/Parakeet-ready with zero conversion. ~115 MB/hour, 32 KB/s.
#define REC_SAMPLE_RATE 16000
#define REC_CHUNK_SIZE  1024  // 64 ms of audio per chunk @ 16 kHz
#define REC_FLUSH_MS    5000  // re-write WAV header every 5 s (power-loss safety)

// SD-write-architecture remediation (PRD §3): a PSRAM ring buffer decouples
// capture (core 1) from the SD-writer task (core 0). The ring absorbs the
// up-to-1.6 s GC stalls that the card can take mid-write, so capture never
// blocks on SD and a stall never loses samples.
#define REC_RING_BYTES   (1024UL * 1024UL)  // 1 MB PSRAM ≈ 30 s @ 32 KB/s (D1)
#define REC_WRITE_BLOCK  (32UL * 1024UL)     // 32 KB drain block (D2)

namespace Page {

// Canonical 44-byte PCM WAV header. Written once with zeroed sizes on start,
// re-flushed periodically, and finalised on stop.
struct __attribute__((packed)) RecWavHeader {
    char riff[4]             = {'R', 'I', 'F', 'F'};
    uint32_t file_size       = 0;
    char wave[4]             = {'W', 'A', 'V', 'E'};
    char fmt[4]              = {'f', 'm', 't', ' '};
    uint32_t fmt_size        = 16;
    uint16_t audio_format    = 1;  // PCM
    uint16_t channels        = 1;
    uint32_t sample_rate     = REC_SAMPLE_RATE;
    uint32_t byte_rate       = REC_SAMPLE_RATE * 2;  // sample_rate * block_align
    uint16_t block_align     = 2;                    // channels * bits/8
    uint16_t bits_per_sample = 16;
    char data[4]             = {'d', 'a', 't', 'a'};
    uint32_t data_size       = 0;
};

class AppRecorderModel {
   public:
    // ES7210 mic <-> AW88298 speaker share the codec; only one runs at a time.
    void MicBegin();  // hand the codec to the mic
    void MicEnd();    // hand it back to the speaker

    bool InitSD();
    bool IsSDCardPresent();  // TF-detect on AW9523 P0.4
    uint64_t SDFreeMB();

    // Lifecycle. StartRecording allocates the ring + spawns the core-0 writer
    // task (which owns the file); StopRecording runs the drain-and-finalise
    // handshake, then tears the ring + task down.
    bool StartRecording();
    void StopRecording();

    // Capture path (core 1, LVGL timer): pull one mic chunk and push it into
    // the ring. Never performs an SD write and never blocks on the bus mutex.
    bool CaptureChunk();

    bool IsRecording()   { return recording; }
    bool HasFault()      { return fault; }            // FR16: drives visible error
    const char* FaultMsg() { return fault_msg; }
    uint32_t RecordingSeconds();                      // FR19: from bytes persisted
    const char* LastFilename() { return last_filename; }
    uint8_t InputLevel() { return level; }            // VU deferred (D5) — stays 0

   private:
    // --- handoff state, capture (core 1) <-> writer (core 0) ----------------
    // Single-writer-per-field; 32-bit aligned access is atomic on Xtensa, so
    // plain volatile is sufficient for these flags/counters (SPSC discipline).
    volatile bool recording        = false;
    volatile bool stop_requested   = false;
    volatile bool writer_done      = false;
    volatile bool fault            = false;
    volatile uint32_t bytes_written = 0;   // PCM bytes persisted to SD (drives ticker)
    char fault_msg[40]             = {0};

    StreamBufferHandle_t ring        = nullptr;
    StaticStreamBuffer_t ring_struct = {};
    uint8_t* ring_storage            = nullptr;  // 1 MB in PSRAM
    TaskHandle_t writer_task         = nullptr;

    File wav_file;                          // writer-task-owned ONLY (D10)
    int file_num          = 1;
    char last_filename[24] = {0};
    int16_t rec_chunk[REC_CHUNK_SIZE];      // capture scratch (internal RAM)
    bool sd_ready          = false;
    uint8_t level          = 0;

    // mic-fail codec-recovery (mic-fail trigger retained per PRD §2.2; the
    // SD-short-write rollover trigger is removed — K4).
    uint32_t mic_fail_streak  = 0;
    uint32_t last_mic_reset_ms = 0;

    int FindNextFileNum();

    // --- writer-task internals (run on core 0) ------------------------------
    void WriteHeader();                       // seek0/write/seek-end, writer-side
    bool OpenNewFile();                        // open + placeholder header
    void WriterLoop();                         // the task body
    static void WriterTrampoline(void* arg);
    void SetFault(const char* msg);
};

// App-level singleton: lives across page navigation so recording survives
// switching to Files / Settings / Menu. Created in App_Init().
extern AppRecorderModel* g_app_recorder_model;

}  // namespace Page

#endif
