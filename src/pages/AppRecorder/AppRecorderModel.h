#ifndef __APPRECORDER_MODEL_H
#define __APPRECORDER_MODEL_H

#include "lvgl.h"
#include "config.h"
#include <SD.h>

// Meeting-recorder capture format: 16 kHz, mono, 16-bit PCM.
// Whisper-ready with zero conversion. ~115 MB/hour.
#define REC_SAMPLE_RATE 16000
#define REC_CHUNK_SIZE  1024  // 64 ms of audio per chunk @ 16 kHz
#define REC_FLUSH_MS    5000  // re-write WAV header every 5 s (power-loss safety)

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

    bool StartRecording();
    void StopRecording();
    bool WriteChunk();  // pull one mic chunk -> SD, update level + byte count

    // Auto-rollover: finalise current file, reset the mic codec, start the next
    // file — recovers from a silent M5.Mic.record() fault without user action.
    // Called automatically from WriteChunk on a sustained failure streak.
    bool RolloverFile();
    bool RolloverHappened();  // returns + clears the pending flag (for the controller)

    bool IsRecording() { return recording; }
    uint32_t RecordingSeconds();
    const char* LastFilename() { return last_filename; }
    uint8_t InputLevel() { return level; }  // 0..100 peak, for the VU bar

   private:
    bool recording        = false;
    File wav_file;
    uint32_t total_bytes  = 0;
    uint32_t rec_start_ms = 0;
    uint32_t last_flush_ms = 0;
    int file_num          = 1;
    int16_t rec_chunk[REC_CHUNK_SIZE];
    char last_filename[24] = {0};
    bool sd_ready          = false;
    uint8_t level          = 0;

    // Auto-rollover state: tracks the consecutive-mic-failure streak that signals
    // the codec has silently faulted, and rate-limits rollover attempts.
    uint32_t mic_fail_streak  = 0;
    uint32_t last_rollover_ms = 0;
    bool rolled_over_pending  = false;

    int FindNextFileNum();
    void WriteHeader();  // seek 0, write current sizes, return to append point
};

}  // namespace Page

#endif
