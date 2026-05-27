#ifndef __APPMIC_MODEL_H
#define __APPMIC_MODEL_H

#include "lvgl.h"
#include "config.h"
#include <SD.h>

#define REC_SAMPLE_RATE 16000
#define REC_CHUNK_SIZE  1024

namespace Page {

struct __attribute__((packed)) WavHeader {
  char riff[4]             = {'R','I','F','F'};
  uint32_t file_size       = 0;
  char wave[4]             = {'W','A','V','E'};
  char fmt[4]              = {'f','m','t',' '};
  uint32_t fmt_size        = 16;
  uint16_t audio_format    = 1;
  uint16_t channels        = 1;
  uint32_t sample_rate     = REC_SAMPLE_RATE;
  uint32_t byte_rate       = REC_SAMPLE_RATE * 2;
  uint16_t block_align     = 2;
  uint16_t bits_per_sample = 16;
  char data[4]             = {'d','a','t','a'};
  uint32_t data_size       = 0;
};

class AppMicModel {
   public:
    int16_t mic_buf[2][MIC_BUF_SIZE];
    int16_t prev_y[MIC_BUF_SIZE];
    uint8_t buf_index = 0;

    bool ReadMicData();
    bool IsMicEnable();
    bool IsSpeakerEnable();
    void MicBegin();
    void MicEnd();

    bool StartRecording();
    void StopRecording();
    bool WriteChunk();
    bool IsRecording() { return recording; }
    uint32_t RecordingSeconds();
    const char* LastFilename() { return last_filename; }

   private:
    bool recording = false;
    File wav_file;
    uint32_t total_bytes = 0;
    uint32_t rec_start_ms = 0;
    int file_num = 1;
    int16_t rec_chunk[REC_CHUNK_SIZE];
    char last_filename[20] = {0};
    bool sd_ready = false;

    bool InitSD();
    int FindNextFileNum();
};

}  // namespace Page

#endif
