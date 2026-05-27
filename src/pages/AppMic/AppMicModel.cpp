#include "AppMicModel.h"
#include "M5Unified.h"

using namespace Page;

bool AppMicModel::ReadMicData() {
    return M5.Mic.record(mic_buf[buf_index], MIC_BUF_SIZE, 16000);
}

bool AppMicModel::IsMicEnable() {
    return M5.Mic.isEnabled();
}

bool AppMicModel::IsSpeakerEnable() {
    return M5.Speaker.isEnabled();
}

void AppMicModel::MicEnd() {
    if (recording) StopRecording();
    M5.Mic.end();
    M5.Speaker.begin();
}

void AppMicModel::MicBegin() {
    M5.Speaker.end();
    M5.Mic.begin();
}

bool AppMicModel::InitSD() {
    if (sd_ready) return true;
    sd_ready = SD.begin(GPIO_NUM_4, SPI, 25000000);
    if (sd_ready) {
        file_num = FindNextFileNum();
        Serial.printf("SD ready, next file: REC_%03d.wav\n", file_num);
    }
    return sd_ready;
}

int AppMicModel::FindNextFileNum() {
    int n = 1;
    char path[20];
    while (true) {
        snprintf(path, sizeof(path), "/REC_%03d.wav", n);
        if (!SD.exists(path)) return n;
        n++;
    }
}

bool AppMicModel::StartRecording() {
    if (recording) return false;
    if (!InitSD()) return false;

    snprintf(last_filename, sizeof(last_filename), "/REC_%03d.wav", file_num);
    wav_file = SD.open(last_filename, FILE_WRITE);
    if (!wav_file) {
        Serial.printf("Failed to open %s\n", last_filename);
        return false;
    }

    WavHeader header;
    wav_file.write((uint8_t *)&header, sizeof(header));

    total_bytes = 0;
    rec_start_ms = millis();
    recording = true;
    Serial.printf("Recording: %s\n", last_filename);
    return true;
}

void AppMicModel::StopRecording() {
    if (!recording) return;
    recording = false;

    WavHeader header;
    header.data_size = total_bytes;
    header.file_size = total_bytes + sizeof(WavHeader) - 8;
    wav_file.seek(0);
    wav_file.write((uint8_t *)&header, sizeof(header));
    wav_file.close();

    Serial.printf("Saved: %s (%u bytes, %us)\n",
        last_filename, total_bytes, total_bytes / (REC_SAMPLE_RATE * 2));
    file_num++;
}

bool AppMicModel::WriteChunk() {
    if (!recording) return false;
    if (M5.Mic.record(rec_chunk, REC_CHUNK_SIZE, REC_SAMPLE_RATE)) {
        wav_file.write((uint8_t *)rec_chunk, REC_CHUNK_SIZE * sizeof(int16_t));
        total_bytes += REC_CHUNK_SIZE * sizeof(int16_t);
        return true;
    }
    return false;
}

uint32_t AppMicModel::RecordingSeconds() {
    if (!recording) return 0;
    return (millis() - rec_start_ms) / 1000;
}
