#include "AppRecorder.h"
#include "../AppPower/AppPowerModel.h"
#include "../../net/RecorderServer.h"
#include <SD.h>

using namespace Page;

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

    // Model is owned by App_Init, not the page. We just attach the mic path if
    // it's not already active. MicBegin() is idempotent for our needs.
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
    // Do NOT call Model()->MicEnd() here. Recording must survive page nav.
}

void AppRecorder::onViewDidUnload() { LV_LOG_USER(__func__); }

bool AppRecorder::StorageFull() {
    if (!Model()) return false;
    // Only called while NOT recording (idle / pre-start) — so no SD-writer task
    // is touching the bus and this core-1 SD access is safe without the mutex.
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

    // Serial-control bench triggers (used by the soak harness): r = start, s = stop.
    while (Serial.available()) {
        char c = Serial.read();
        if (c == 'r' && !Model()->IsRecording() && !StorageFull()) {
            last_sec = 0;
            if (Model()->StartRecording()) {
                Net::Server.setRecording(true);
                View.SetRecording(0);
            } else {
                View.SetError(Model()->HasFault() ? Model()->FaultMsg() : "SD card error");
            }
        } else if (c == 's' && Model()->IsRecording()) {
            Model()->StopRecording();
            Net::Server.setRecording(false);
            Net::Server.requestNotify();
            View.SetSaved(Model()->LastFilename(), last_sec);
        } else if (c == 'p') {
            // Bench diagnostic: dump the AXP2101 power-key timing register so the
            // FR-PWRON quick-tap config can be verified on demand (boot-time
            // capture is unreliable on the CoreS3 USB-Serial/JTAG re-enum).
            AppPowerModel pm;
            uint8_t v = pm.ReadPowerKeyReg();
            static const char* onl[] = {"128ms", "512ms", "1s", "2s"};
            Serial.printf("[PWR] power-key reg 0x27=0x%02X  ONLEVEL=%s\n",
                          v, onl[v & 0x03]);
            Serial.flush();
        } else if (c == 'b') {
            // Bench diagnostic: dump battery / power-source state (vbat, vbus,
            // charge, BATFET) to diagnose battery-only power-on.
            AppPowerModel pm;
            pm.DumpPowerState();
        }
    }

    // FR16: a writer/capture fault (overrun or hard SD fault) stops the recording
    // and shows a visible, sticky error — never a silent stop. Sticky until the
    // user taps Record again (StartRecording clears the fault).
    if (Model()->HasFault()) {
        if (Model()->IsRecording()) {
            Model()->StopRecording();   // runs the drain/finalise handshake
            Net::Server.setRecording(false);
        }
        View.SetError(Model()->FaultMsg());
        last_sec = 0;
        return;
    }

    if (!Model()->IsRecording()) {
        // FR33: re-check storage-full when idle (covers files deleted via the
        // Files page). Throttled to ~1 Hz — SD.totalBytes/usedBytes walk the FAT.
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

    // Recording: pull a mic chunk into the PSRAM ring (the core-0 writer task
    // drains it to SD). This never blocks on SD or the bus.
    Model()->CaptureChunk();

    // FR30-FR32: critical-battery auto-save during recording. StopRecording runs
    // the writer drain/finalise handshake before power-off (no corrupt file).
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

    // FR19: elapsed-time ticker, driven from audio actually persisted. Redraws
    // only when the whole second changes (≥1 Hz, minimal LCD bus-grabs). VU
    // meter is deferred (D5) — no per-frame level redraw during recording.
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
                instance->View.SetError(instance->Model()->HasFault()
                                            ? instance->Model()->FaultMsg()
                                            : "SD card error");
            }
        }
    }
}
