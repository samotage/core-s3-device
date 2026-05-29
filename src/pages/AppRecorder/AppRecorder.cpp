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
    uint64_t free_bytes = (uint64_t)SD.totalBytes() - (uint64_t)SD.usedBytes();
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
        }
    }

    if (!Model()->IsRecording()) {
        // FR33: re-check storage-full state when idle so the screen updates if
        // the user just deleted files via the Files page.
        bool full = StorageFull();
        if (full && !last_storage_full) {
            last_storage_full = true;
            View.SetStorageFull();
        } else if (!full && last_storage_full) {
            last_storage_full = false;
            View.SetIdle();
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
