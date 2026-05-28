#include "AppRecorderModel.h"
#include "M5Unified.h"

using namespace Page;

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
    sd_ready = SD.begin(GPIO_NUM_4, SPI, 25000000);
    if (sd_ready) {
        file_num = FindNextFileNum();
        Serial.printf("[REC] SD ready, next file: REC_%03d.wav\n", file_num);
    } else {
        Serial.println("[REC] SD mount failed");
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
        // Diagnostic: surface silent mic failures. This was the cause of the
        // ~6-min cut-off Sam saw — mic stops producing samples, recording state
        // stays true, timer kept counting wall-clock while no bytes were
        // written. Throttled to one log per 5 s so serial isn't flooded.
        static uint32_t mic_fail_count = 0;
        static uint32_t last_log_ms    = 0;
        mic_fail_count++;
        if (millis() - last_log_ms > 5000) {
            Serial.printf("[REC] mic.record FAIL  count=%u  total_bytes=%u  "
                          "audio_secs=%u\n",
                          mic_fail_count, total_bytes,
                          total_bytes / (REC_SAMPLE_RATE * 2));
            last_log_ms = millis();
        }
        return false;
    }

    wav_file.write((uint8_t*)rec_chunk, REC_CHUNK_SIZE * sizeof(int16_t));
    total_bytes += REC_CHUNK_SIZE * sizeof(int16_t);

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
