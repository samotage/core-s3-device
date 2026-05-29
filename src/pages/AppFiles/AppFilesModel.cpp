#include "AppFilesModel.h"
#include "recorder_math.h"
#include <SD.h>
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

using namespace Page;

static int cmp_names_24(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b);
}

uint32_t AppFilesModel::EstimatedMinutes(uint64_t free_bytes) {
    return RecorderMath::EstimatedMinutes(free_bytes);
}

// Walks the SD root and counts files whose name starts with "REC_" and ends
// in ".wav". Only the canonical recording files.
bool AppFilesModel::CollectStats(FilesStats& out) {
    out = FilesStats{};
    if (!SD.cardType()) return false;

    out.total_bytes = SD.totalBytes();
    out.used_bytes  = SD.usedBytes();
    uint64_t free_bytes = (out.total_bytes > out.used_bytes)
                              ? (out.total_bytes - out.used_bytes) : 0;
    out.est_minutes_left = EstimatedMinutes(free_bytes);

    File root = SD.open("/");
    if (!root) return false;
    File entry;
    while ((entry = root.openNextFile())) {
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            // Some SD libs return "/REC_001.wav", some "REC_001.wav".
            const char* base = name;
            const char* slash = strrchr(name, '/');
            if (slash) base = slash + 1;
            if (strncmp(base, "REC_", 4) == 0) {
                size_t len = strlen(base);
                if (len >= 4 && strcmp(base + len - 4, ".wav") == 0) {
                    out.file_count++;
                }
            }
        }
        entry.close();
    }
    root.close();
    return true;
}

int AppFilesModel::DeleteOldestRecordings(int n) {
    if (n <= 0) return 0;
    if (!SD.cardType()) return 0;

    // Collect REC_*.wav names into a small dynamic array. Cap at 256 entries
    // to keep RAM bounded — at one recording per meeting that's many months
    // of history; well over the "oldest 10" use case.
    const size_t MAX = 256;
    char (*names)[24] = (char(*)[24])calloc(MAX, sizeof(*names));
    if (!names) return 0;
    size_t count = 0;

    File root = SD.open("/");
    if (!root) { free(names); return 0; }
    File entry;
    while ((entry = root.openNextFile()) && count < MAX) {
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            const char* base = name;
            const char* slash = strrchr(name, '/');
            if (slash) base = slash + 1;
            if (strncmp(base, "REC_", 4) == 0) {
                size_t len = strlen(base);
                if (len >= 4 && strcmp(base + len - 4, ".wav") == 0 && len < sizeof(names[0])) {
                    strncpy(names[count], base, sizeof(names[count]) - 1);
                    names[count][sizeof(names[count]) - 1] = 0;
                    count++;
                }
            }
        }
        entry.close();
    }
    root.close();

    // Sort by filename — REC_001.wav < REC_002.wav < ... so the prefix is
    // the natural "oldest first" order. Use C qsort to avoid issues with
    // std::sort and C-array element types.
    qsort(names, count, sizeof(names[0]), cmp_names_24);

    int to_delete = (int)count < n ? (int)count : n;
    int removed = 0;
    char path[32];
    for (int i = 0; i < to_delete; ++i) {
        snprintf(path, sizeof(path), "/%s", names[i]);
        if (SD.remove(path)) {
            removed++;
            Serial.printf("[FILES] removed %s\n", path);
        } else {
            Serial.printf("[FILES] FAILED to remove %s\n", path);
        }
    }
    free(names);
    return removed;
}
