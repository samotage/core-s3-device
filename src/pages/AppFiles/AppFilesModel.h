#ifndef __APPFILES_MODEL_H
#define __APPFILES_MODEL_H

#include "lvgl.h"
#include <stdint.h>

namespace Page {

// Pure-data summary the view consumes.
struct FilesStats {
    uint32_t file_count       = 0;
    uint64_t used_bytes       = 0;
    uint64_t total_bytes      = 0;
    uint32_t est_minutes_left = 0;  // at 16 kHz mono 16-bit (~115 MB/hour)
};

class AppFilesModel {
   public:
    // ~115 MB/hour at 16 kHz mono 16-bit (sample_rate*2 bytes/sec).
    // Exposed so unit tests can use the same constant.
    static constexpr uint64_t kBytesPerSecond = 32000ULL;  // 16000 * 2

    // Compute est. recording minutes remaining from free space at the
    // canonical record rate (FR15). Pure function for testability.
    static uint32_t EstimatedMinutes(uint64_t free_bytes);

    bool CollectStats(FilesStats& out);  // walks SD root for REC_*.wav

    // Returns the count actually removed (PRD: 10 oldest by filename sort).
    int DeleteOldestRecordings(int n);
};

}  // namespace Page

#endif
