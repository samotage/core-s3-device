#include "AppRecorderModel.h"
#include "M5Unified.h"

using namespace Page;

// App-level recording-model singleton. Defined here, declared extern in the
// header. App_Init() assigns; pages reference via the pointer.
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
    Serial.println("[REC] mounting SD (will auto-format FAT32/MBR if unmountable)...");
    Serial.flush();
    // 6th arg = format_if_empty=true: when the FATFS mount fails, ESP-IDF will
    // create a fresh FAT32 partition with an MBR table — the canonical SD
    // layout the Arduino-ESP32 SD library expects. Handles cards the Mac
    // formats in incompatible ways (FAT32-on-GPT, unusual cluster geometry,
    // corrupt cluster chains). On a 256 GB card the format itself can take
    // a while; the UI will block until it completes.
    sd_ready = SD.begin(GPIO_NUM_4, SPI, 25000000, "/sd", 5, /*format_if_empty=*/true);
    if (sd_ready) {
        file_num = FindNextFileNum();
        uint64_t free_mb = SDFreeMB();
        Serial.printf("[REC] SD ready, next file: REC_%03d.wav  (free: %llu MB)\n",
                      file_num, free_mb);
    } else {
        Serial.println("[REC] SD mount failed (even after attempted format)");
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

// Write the WAV header with current sizes, then return the file pointer to the
// append point. Called on stop and periodically during recording so a yanked
// cable still leaves a playable file (losing at most REC_FLUSH_MS of audio).
void AppRecorderModel::WriteHeader() {
    RecWavHeader header;
    header.data_size = total_bytes;
    header.file_size = total_bytes + sizeof(RecWavHeader) - 8;
    wav_file.seek(0);
    wav_file.write((uint8_t*)&header, sizeof(header));
    wav_file.seek(sizeof(RecWavHeader) + total_bytes);
}

bool AppRecorderModel::StartRecording() {
    if (recording) return false;
    if (!InitSD()) return false;

    snprintf(last_filename, sizeof(last_filename), "/REC_%03d.wav", file_num);
    wav_file = SD.open(last_filename, FILE_WRITE);
    if (!wav_file) {
        Serial.printf("[REC] Failed to open %s\n", last_filename);
        return false;
    }

    // Placeholder header (zeroed sizes) — finalised on stop.
    RecWavHeader header;
    wav_file.write((uint8_t*)&header, sizeof(header));

    total_bytes   = 0;
    level         = 0;
    rec_start_ms  = millis();
    last_flush_ms = rec_start_ms;
    recording     = true;
    Serial.printf("[REC] Recording -> %s\n", last_filename);
    return true;
}

void AppRecorderModel::StopRecording() {
    if (!recording) return;
    recording = false;
    level     = 0;

    WriteHeader();
    wav_file.close();
    Serial.printf("[REC] Saved %s (%u bytes, %us)\n", last_filename, total_bytes,
                  total_bytes / (REC_SAMPLE_RATE * 2));
    file_num++;
}

bool AppRecorderModel::WriteChunk() {
    if (!recording) return false;
    if (!M5.Mic.record(rec_chunk, REC_CHUNK_SIZE, REC_SAMPLE_RATE)) {
        // Silent mic failure — the cause of the ~6-min cut-off. Track the streak;
        // if it persists, auto-rollover (finalise this file, reset codec, start
        // the next file) so the meeting keeps recording without user action.
        mic_fail_streak++;
        static uint32_t last_log_ms = 0;
        if (millis() - last_log_ms > 5000) {
            Serial.printf("[REC] mic.record FAIL  streak=%u  total_bytes=%u  "
                          "audio_secs=%u\n",
                          mic_fail_streak, total_bytes,
                          total_bytes / (REC_SAMPLE_RATE * 2));
            last_log_ms = millis();
        }
        // ~30 consecutive failures @ 33 ms timer ≈ 1 s of dead air → roll over.
        // Rate-limited so we don't tight-loop if the recovery itself fails.
        const uint32_t FAIL_THRESHOLD = 30;
        const uint32_t ROLLOVER_MIN_INTERVAL_MS = 10000;
        if (mic_fail_streak >= FAIL_THRESHOLD &&
            millis() - last_rollover_ms >= ROLLOVER_MIN_INTERVAL_MS) {
            RolloverFile();
        }
        return false;
    }
    // Successful read clears the streak.
    mic_fail_streak = 0;

    // SD safety: check the SD write actually persisted the bytes. The crash-
    // corrupted-FAT scenario (50-min YouTube recording lost) returned
    // "success" from SD.open + claimed bytes written, but the file was 0 on
    // disk. If write() ever returns short, stop the recording immediately so
    // we don't capture into the void.
    const size_t want = REC_CHUNK_SIZE * sizeof(int16_t);
    const size_t got  = wav_file.write((uint8_t*)rec_chunk, want);
    if (got != want) {
        Serial.printf("[REC] SD WRITE SHORT — expected %u got %u — STOPPING "
                      "(filesystem fault, total so far %u bytes)\n",
                      (unsigned)want, (unsigned)got, total_bytes);
        recording = false;  // truthful timer freezes; controller next tick sees idle
        return false;
    }
    total_bytes += got;

    // Peak level for the VU meter (0..100).
    int32_t peak = 0;
    for (size_t i = 0; i < REC_CHUNK_SIZE; ++i) {
        int32_t a = rec_chunk[i] < 0 ? -rec_chunk[i] : rec_chunk[i];
        if (a > peak) peak = a;
    }
    uint32_t pct = (uint32_t)peak * 100 / 32768;
    level = pct > 100 ? 100 : (uint8_t)pct;

    // Periodic header flush for power-loss safety.
    uint32_t now = millis();
    if (now - last_flush_ms >= REC_FLUSH_MS) {
        WriteHeader();
        wav_file.flush();
        last_flush_ms = now;
    }
    return true;
}

uint32_t AppRecorderModel::RecordingSeconds() {
    if (!recording) return 0;
    // Return actual audio captured (not wall-clock), so the UI never lies if
    // the mic silently stops producing samples — the timer will freeze at the
    // last good second instead of climbing while no bytes are being written.
    return total_bytes / (REC_SAMPLE_RATE * 2);
}

// Recover from a silent mic-codec fault mid-recording: finalise the current
// file, reset the I2S/mic path (without touching the speaker — that would
// release the codec entirely), and open the next REC_NNN.wav. The meeting
// continues as a chain of back-to-back files, no user action required.
bool AppRecorderModel::RolloverFile() {
    if (!recording) return false;
    Serial.printf("[REC] AUTO-ROLLOVER triggered  streak=%u  bytes=%u\n",
                  mic_fail_streak, total_bytes);

    StopRecording();           // finalises the WAV, increments file_num

    M5.Mic.end();
    delay(50);                 // brief settle so the I2S driver tears down clean
    M5.Mic.begin();

    bool ok = StartRecording();
    if (ok) {
        mic_fail_streak     = 0;
        last_rollover_ms    = millis();
        rolled_over_pending = true;  // signal controller to notify the new file
        Serial.printf("[REC] AUTO-ROLLOVER -> %s\n", last_filename);
    } else {
        Serial.println("[REC] AUTO-ROLLOVER failed (StartRecording returned false)");
    }
    return ok;
}

bool AppRecorderModel::RolloverHappened() {
    bool r = rolled_over_pending;
    rolled_over_pending = false;
    return r;
}
