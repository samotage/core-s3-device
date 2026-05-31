#include "AppRecorder.h"
#include "../AppPower/AppPowerModel.h"
#include "../../net/RecorderServer.h"
#include <SD.h>
#include <SPI.h>

using namespace Page;

// [SDTEST] Diagnostic-only SD write stress test, triggered by serial 'T'.
// Hammers the card with the recorder's exact 2048-byte chunked-write pattern
// (incl. the 5s flush cadence) across descending SPI clocks, isolating the SD
// path from the mic/LVGL pipeline. Reports throughput + the exact byte offset
// of any short write. Remove once the SD cut-off root cause is nailed.
static void RunSDTest() {
    static uint8_t buf[2048];
    for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = (uint8_t)(i & 0xFF);
    const uint32_t speeds[] = {25000000, 20000000, 16000000, 10000000, 4000000};
    const uint32_t TARGET = 20UL * 1024 * 1024;  // 20 MB/speed (~10.5 min audio-equiv)

    Serial.println("\n[SDTEST] ===== SD WRITE STRESS TEST START =====");
    Serial.flush();
    for (uint32_t sp : speeds) {
        Serial.printf("[SDTEST] ---- SPI %lu Hz ----\n", (unsigned long)sp); Serial.flush();
        SD.end();
        delay(150);
        if (!SD.begin(GPIO_NUM_4, SPI, sp)) {
            Serial.printf("[SDTEST] %lu: SD.begin() FAILED to mount\n", (unsigned long)sp);
            Serial.flush();
            continue;
        }
        Serial.printf("[SDTEST] %lu: mounted total=%lluMB used=%lluMB\n", (unsigned long)sp,
                      (unsigned long long)(SD.totalBytes() / 1048576ULL),
                      (unsigned long long)(SD.usedBytes() / 1048576ULL));
        Serial.flush();
        SD.remove("/SDTEST.bin");
        File f = SD.open("/SDTEST.bin", FILE_WRITE);
        if (!f) { Serial.printf("[SDTEST] %lu: open FAILED\n", (unsigned long)sp); Serial.flush(); continue; }

        uint32_t total = 0, chunks = 0; bool failed = false;
        uint32_t t0 = millis(), last = t0;
        while (total < TARGET) {
            size_t got = f.write(buf, sizeof(buf));
            ++chunks;
            if (got != sizeof(buf)) {
                Serial.printf("[SDTEST] %lu: *** SHORT WRITE *** chunk=%lu got=%u at %lu bytes (%lu KB)\n",
                              (unsigned long)sp, (unsigned long)chunks, (unsigned)got,
                              (unsigned long)total, (unsigned long)(total / 1024));
                Serial.flush(); failed = true; break;
            }
            total += got;
            if (millis() - last >= 5000) {            // mirror the recorder's 5s header flush
                last = millis();
                f.flush();
                uint32_t dt = millis() - t0;
                Serial.printf("[SDTEST] %lu: %lu MB ok, ~%lu KB/s\n", (unsigned long)sp,
                              (unsigned long)(total / 1048576UL),
                              dt ? (unsigned long)((uint64_t)total / dt) : 0);
                Serial.flush();
            }
        }
        f.flush(); f.close();
        uint32_t dt = millis() - t0;
        Serial.printf("[SDTEST] %lu: RESULT %s wrote %lu KB in %lus (~%lu KB/s)\n",
                      (unsigned long)sp, failed ? "FAIL" : "PASS",
                      (unsigned long)(total / 1024), (unsigned long)(dt / 1000),
                      dt ? (unsigned long)((uint64_t)total / dt) : 0);
        Serial.flush();
        SD.remove("/SDTEST.bin");
    }
    Serial.println("[SDTEST] ===== DONE (reset device to resume recorder) ====="); Serial.flush();
}

// [SDTEST2] Same write load at 25 MHz, but replicating the recorder's WAV-header
// rewrite: every 5s seek to byte 0, rewrite the 44-byte header, seek back to the
// end. This is the one thing the plain stress test omits. If THIS short-writes,
// the seek-to-front-of-growing-file pattern is the cut-off cause. Triggered by 'U'.
static void RunSDTestSeek() {
    static uint8_t buf[2048];
    for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = (uint8_t)(i & 0xFF);
    uint8_t hdr[44] = {0};
    const uint32_t TARGET = 20UL * 1024 * 1024;
    Serial.println("\n[SDTEST2] ===== SD WRITE + WAV-HEADER-SEEK (25MHz) ====="); Serial.flush();
    SD.end(); delay(150);
    if (!SD.begin(GPIO_NUM_4, SPI, 25000000)) { Serial.println("[SDTEST2] mount FAILED"); Serial.flush(); return; }
    SD.remove("/SDTEST2.wav");
    File f = SD.open("/SDTEST2.wav", FILE_WRITE);
    if (!f) { Serial.println("[SDTEST2] open FAILED"); Serial.flush(); return; }
    f.write(hdr, 44);  // placeholder header, like StartRecording()
    uint32_t total = 0, chunks = 0, seeks = 0; bool failed = false;
    uint32_t t0 = millis(), last = t0;
    while (total < TARGET) {
        size_t got = f.write(buf, sizeof(buf));
        ++chunks;
        if (got != sizeof(buf)) {
            Serial.printf("[SDTEST2] *** SHORT WRITE *** chunk=%lu got=%u at %lu KB (after %lu header-seeks)\n",
                          (unsigned long)chunks, (unsigned)got, (unsigned long)(total / 1024), (unsigned long)seeks);
            Serial.flush(); failed = true; break;
        }
        total += got;
        if (millis() - last >= 5000) {            // exact WriteHeader() pattern
            last = millis();
            f.seek(0);
            size_t hw = f.write(hdr, 44);
            f.seek(44 + total);
            f.flush();
            ++seeks;
            if (hw != 44) {
                Serial.printf("[SDTEST2] *** HEADER SHORT WRITE *** got=%u at %lu KB\n",
                              (unsigned)hw, (unsigned long)(total / 1024));
                Serial.flush(); failed = true; break;
            }
            uint32_t dt = millis() - t0;
            Serial.printf("[SDTEST2] %lu KB ok (%lu seeks) ~%lu KB/s\n", (unsigned long)(total / 1024),
                          (unsigned long)seeks, dt ? (unsigned long)((uint64_t)total / dt) : 0);
            Serial.flush();
        }
    }
    f.flush(); f.close();
    Serial.printf("[SDTEST2] RESULT %s wrote %lu KB, %lu header-seeks\n",
                  failed ? "FAIL" : "PASS", (unsigned long)(total / 1024), (unsigned long)seeks);
    Serial.flush();
    SD.remove("/SDTEST2.wav");
    Serial.println("[SDTEST2] ===== DONE ====="); Serial.flush();
}

// [SDTEST4] SD writes + an LCD draw every chunk (no mic). Tests whether LCD bus
// traffic (different driver stack, shared SPI) corrupts interleaved SD writes.
// Triggered by 'V'.
static void RunSDTestDisp() {
    static uint8_t buf[2048];
    for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = (uint8_t)(i & 0xFF);
    uint8_t hdr[44] = {0};
    const uint32_t TARGET = 20UL * 1024 * 1024;
    Serial.println("\n[SDTEST4] ===== SD + LCD-DRAW every chunk (no mic) 25MHz ====="); Serial.flush();
    SD.end(); delay(150);
    if (!SD.begin(GPIO_NUM_4, SPI, 25000000)) { Serial.println("[SDTEST4] mount FAILED"); Serial.flush(); return; }
    SD.remove("/SDTEST4.wav");
    File f = SD.open("/SDTEST4.wav", FILE_WRITE);
    if (!f) { Serial.println("[SDTEST4] open FAILED"); Serial.flush(); return; }
    f.write(hdr, 44);
    uint32_t total = 0, chunks = 0; bool failed = false; uint32_t t0 = millis(), last = t0; uint16_t col = 0;
    while (total < TARGET) {
        col += 0x0841;                                  // a VU/timer-style LCD redraw over the shared bus
        M5.Display.fillRect(8, 210, 60, 10, col);
        size_t got = f.write(buf, sizeof(buf));
        ++chunks;
        if (got != sizeof(buf)) {
            Serial.printf("[SDTEST4] *** SD SHORT WRITE *** chunk=%lu got=%u at %lu KB\n",
                          (unsigned long)chunks, (unsigned)got, (unsigned long)(total / 1024));
            Serial.flush(); failed = true; break;
        }
        total += got;
        if (millis() - last >= 5000) {
            last = millis(); f.seek(0); f.write(hdr, 44); f.seek(44 + total); f.flush();
            uint32_t dt = millis() - t0;
            Serial.printf("[SDTEST4] %lu KB ok ~%lu KB/s\n", (unsigned long)(total / 1024),
                          dt ? (unsigned long)((uint64_t)total / dt) : 0);
            Serial.flush();
        }
    }
    f.flush(); f.close();
    Serial.printf("[SDTEST4] RESULT %s wrote %lu KB\n", failed ? "FAIL" : "PASS", (unsigned long)(total / 1024));
    Serial.flush();
    SD.remove("/SDTEST4.wav");
    Serial.println("[SDTEST4] ===== DONE ====="); Serial.flush();
}

// [SDTEST3] The real recording loop minus the display: live M5.Mic.record() +
// SD write + 5s header seek, paced by the mic (~real-time). Triggered by 'W'.
// TARGET 16 MB (~8.3 min) — long, but the cut-off historically hits by ~6.5 min.
static void RunSDTestMic() {
    static int16_t mic[1024];
    uint8_t hdr[44] = {0};
    const uint32_t TARGET = 16UL * 1024 * 1024;
    Serial.println("\n[SDTEST3] ===== SD + LIVE MIC (no display) 25MHz ====="); Serial.flush();
    M5.Speaker.end(); M5.Mic.begin();
    SD.end(); delay(150);
    if (!SD.begin(GPIO_NUM_4, SPI, 25000000)) { Serial.println("[SDTEST3] mount FAILED"); Serial.flush(); M5.Mic.end(); M5.Speaker.begin(); return; }
    SD.remove("/SDTEST3.wav");
    File f = SD.open("/SDTEST3.wav", FILE_WRITE);
    if (!f) { Serial.println("[SDTEST3] open FAILED"); Serial.flush(); M5.Mic.end(); M5.Speaker.begin(); return; }
    f.write(hdr, 44);
    uint32_t total = 0, chunks = 0, micfail = 0; bool failed = false; uint32_t t0 = millis(), last = t0;
    while (total < TARGET) {
        if (!M5.Mic.record(mic, 1024, 16000)) ++micfail;   // paces the loop to real-time
        size_t got = f.write((uint8_t*)mic, 2048);
        ++chunks;
        if (got != 2048) {
            Serial.printf("[SDTEST3] *** SD SHORT WRITE *** chunk=%lu got=%u at %lu KB (micfails=%lu)\n",
                          (unsigned long)chunks, (unsigned)got, (unsigned long)(total / 1024), (unsigned long)micfail);
            Serial.flush(); failed = true; break;
        }
        total += got;
        if (millis() - last >= 5000) {
            last = millis(); f.seek(0); f.write(hdr, 44); f.seek(44 + total); f.flush();
            uint32_t dt = millis() - t0;
            Serial.printf("[SDTEST3] %lu KB ok, micfails=%lu, ~%lu KB/s\n", (unsigned long)(total / 1024),
                          (unsigned long)micfail, dt ? (unsigned long)((uint64_t)total / dt) : 0);
            Serial.flush();
        }
    }
    f.flush(); f.close();
    Serial.printf("[SDTEST3] RESULT %s wrote %lu KB micfails=%lu\n", failed ? "FAIL" : "PASS",
                  (unsigned long)(total / 1024), (unsigned long)micfail);
    Serial.flush();
    SD.remove("/SDTEST3.wav");
    M5.Mic.end(); M5.Speaker.begin();
    Serial.println("[SDTEST3] ===== DONE ====="); Serial.flush();
}

AppRecorder::AppRecorder()
    : timer(nullptr), last_sec(0), last_storage_full(false) {}

AppRecorder::~AppRecorder() {}

void AppRecorder::onCustomAttrConfig() { LV_LOG_USER(__func__); }

void AppRecorder::onViewLoad() {
    LV_LOG_USER(__func__);
    View.Create(_root);

    AttachEvent(_root);
    AttachEvent(View.ui.btn_record,   LV_EVENT_CLICKED);
    AttachEvent(View.ui.btn_menu,     LV_EVENT_CLICKED);
    AttachEvent(View.ui.btn_to_files, LV_EVENT_CLICKED);

    // FR35: model is owned by App_Init, not the page. We just attach the mic
    // path if it's not already active. MicBegin() is idempotent for our needs.
    if (Model()) Model()->MicBegin();
    ShowIdle();
}

void AppRecorder::onViewDidLoad() { LV_LOG_USER(__func__); }

void AppRecorder::onViewWillAppear() {
    LV_LOG_USER(__func__);
    timer = lv_timer_create(onTimerUpdate, 33, this);  // mic DMA cadence
}

void AppRecorder::onViewDidAppear()    { LV_LOG_USER(__func__); }
void AppRecorder::onViewWillDisappear(){ LV_LOG_USER(__func__); }
void AppRecorder::onViewDidDisappear() { LV_LOG_USER(__func__); lv_timer_del(timer); }

void AppRecorder::onViewUnload() {
    LV_LOG_USER(__func__);
    View.Delete();
    // FR35: do NOT call Model()->MicEnd() here. Recording must survive page nav.
}

void AppRecorder::onViewDidUnload() { LV_LOG_USER(__func__); }

bool AppRecorder::StorageFull() {
    if (!Model()) return false;
    // Skip if SD not yet ready — InitSD() returns false and we handle below.
    if (!Model()->IsSDCardPresent()) return false;
    if (!Model()->InitSD()) return false;
    uint64_t total = (uint64_t)SD.totalBytes();
    uint64_t used  = (uint64_t)SD.usedBytes();
    uint64_t free_bytes = (total > used) ? (total - used) : 0;  // guard underflow
    return free_bytes < STORAGE_FULL_THRESHOLD_BYTES;
}

void AppRecorder::ShowIdle() {
    if (!Model()) return;
    if (Model()->IsRecording()) {
        // Came back to the page while a recording is in progress.
        last_sec = Model()->RecordingSeconds();
        View.SetRecording(last_sec);
        return;
    }
    bool present = Model()->IsSDCardPresent();
    if (!present) {
        View.SetError("Insert SD card");
        return;
    }
    if (!Model()->InitSD()) {
        View.SetError("SD card error");
        return;
    }
    if (StorageFull()) {
        last_storage_full = true;
        View.SetStorageFull();
        return;
    }
    last_storage_full = false;
    View.SetIdle();
}

void AppRecorder::AttachEvent(lv_obj_t* obj, lv_event_code_t code) {
    lv_obj_set_user_data(obj, this);
    lv_obj_add_event_cb(obj, onEvent, code, this);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void AppRecorder::Update() {
    if (!Model()) return;

    // Serial-control bench triggers (unchanged).
    while (Serial.available()) {
        char c = Serial.read();
        if (c == 'r' && !Model()->IsRecording() && !StorageFull()) {
            last_sec = 0;
            if (Model()->StartRecording()) {
                Net::Server.setRecording(true);
                View.SetRecording(0);
            } else {
                View.SetError(LV_SYMBOL_WARNING " SD card error");
            }
        } else if (c == 's' && Model()->IsRecording()) {
            Model()->StopRecording();
            Net::Server.setRecording(false);
            Net::Server.requestNotify();
            View.SetSaved(Model()->LastFilename(), last_sec);
        } else if (c == 'T' && !Model()->IsRecording()) {
            RunSDTest();      // [SDTEST]  plain append-write stress, all SPI speeds
        } else if (c == 'U' && !Model()->IsRecording()) {
            RunSDTestSeek();  // [SDTEST2] adds the WAV-header seek-rewrite pattern
        } else if (c == 'V' && !Model()->IsRecording()) {
            RunSDTestDisp();  // [SDTEST4] SD + LCD draw (isolate display/bus)
        } else if (c == 'W' && !Model()->IsRecording()) {
            RunSDTestMic();   // [SDTEST3] SD + live mic (real loop minus display)
        }
    }

    if (!Model()->IsRecording()) {
        // FR33: re-check storage-full state when idle so the screen updates if
        // the user just deleted files via the Files page. Throttle to ~1Hz —
        // the Update() timer runs at the 33ms mic-DMA cadence, but SD.totalBytes
        // / usedBytes are expensive (FAT cluster walk); polling them every
        // tick while idle is wasted work.
        static uint32_t last_storage_check_ms = 0;
        uint32_t now_ms = millis();
        if (now_ms - last_storage_check_ms >= 1000) {
            last_storage_check_ms = now_ms;
            bool full = StorageFull();
            if (full && !last_storage_full) {
                last_storage_full = true;
                View.SetStorageFull();
            } else if (!full && last_storage_full) {
                last_storage_full = false;
                View.SetIdle();
            }
        }
        return;
    }

    Model()->WriteChunk();

    if (Model()->RolloverHappened()) {
        Net::Server.requestNotify();
        last_sec = 0;
    }

    // FR30-FR32: critical-battery auto-save during recording.
    static uint32_t last_batt_check = 0;
    uint32_t now = millis();
    if (now - last_batt_check >= 5000) {
        last_batt_check = now;
        AppPowerModel pm;
        if (pm.IsCriticalBattery()) {
            Serial.println("[REC] CRITICAL BATTERY — auto-save + shutdown");
            Model()->StopRecording();
            Net::Server.setRecording(false);
            pm.PowerOff();
        }
    }

    View.SetLevel(Model()->InputLevel());

    uint32_t sec = Model()->RecordingSeconds();
    if (sec != last_sec) {
        last_sec = sec;
        View.SetRecording(sec);
    }
}

void AppRecorder::onTimerUpdate(lv_timer_t* timer) {
    AppRecorder* instance = (AppRecorder*)timer->user_data;
    instance->Update();
}

void AppRecorder::onEvent(lv_event_t* event) {
    AppRecorder* instance = (AppRecorder*)lv_event_get_user_data(event);
    LV_ASSERT_NULL(instance);

    lv_obj_t* obj        = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED) return;

    if (obj == instance->View.ui.btn_menu) {
        instance->_Manager->Replace("Pages/HomeMenu");
        return;
    }
    if (obj == instance->View.ui.btn_to_files) {
        instance->_Manager->Replace("Pages/AppFiles");
        return;
    }
    if (obj == instance->View.ui.btn_record) {
        if (!instance->Model()) return;
        if (instance->Model()->IsRecording()) {
            instance->Model()->StopRecording();
            Net::Server.setRecording(false);
            Net::Server.requestNotify();
            instance->View.SetSaved(instance->Model()->LastFilename(),
                                    instance->last_sec);
        } else {
            if (instance->StorageFull()) {
                instance->View.SetStorageFull();
                return;
            }
            instance->last_sec = 0;
            if (instance->Model()->StartRecording()) {
                Net::Server.setRecording(true);
                instance->View.SetRecording(0);
            } else {
                instance->View.SetError(LV_SYMBOL_WARNING " SD card error");
            }
        }
    }
}
