#include "AppRecorder.h"
#include "../../net/RecorderServer.h"

using namespace Page;

AppRecorder::AppRecorder() : timer(nullptr), last_sec(0), last_wifi(-1) {}

AppRecorder::~AppRecorder() {}

void AppRecorder::onCustomAttrConfig() {
    LV_LOG_USER(__func__);
}

void AppRecorder::onViewLoad() {
    LV_LOG_USER(__func__);
    View.Create(_root);

    AttachEvent(_root);
    AttachEvent(View.ui.btn_record, LV_EVENT_CLICKED);

    Model.MicBegin();
    ShowIdle();
}

void AppRecorder::onViewDidLoad() {
    LV_LOG_USER(__func__);
}

void AppRecorder::onViewWillAppear() {
    LV_LOG_USER(__func__);
    // ~33ms cadence: fast enough to drain the mic DMA buffer in real time.
    timer = lv_timer_create(onTimerUpdate, 33, this);
}

void AppRecorder::onViewDidAppear() {
    LV_LOG_USER(__func__);
}

void AppRecorder::onViewWillDisappear() {
    LV_LOG_USER(__func__);
}

void AppRecorder::onViewDidDisappear() {
    LV_LOG_USER(__func__);
    lv_timer_del(timer);
}

void AppRecorder::onViewUnload() {
    LV_LOG_USER(__func__);
    View.Delete();
    Model.MicEnd();
}

void AppRecorder::onViewDidUnload() {
    LV_LOG_USER(__func__);
}

void AppRecorder::ShowIdle() {
    bool present = Model.IsSDCardPresent();
    if (present && Model.InitSD()) {
        View.SetIdle(true, Model.SDFreeMB());
    } else {
        View.SetIdle(present, 0);
    }
}

void AppRecorder::AttachEvent(lv_obj_t* obj, lv_event_code_t code) {
    lv_obj_set_user_data(obj, this);
    lv_obj_add_event_cb(obj, onEvent, code, this);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void AppRecorder::Update() {
    // Serial debug trigger — bench-test only ('r' = start, 's' = stop).
    // Same code path as the touch button. Not a network-facing surface; the
    // over-the-network remote-start feature is parked in issue #1.
    while (Serial.available()) {
        char c = Serial.read();
        if (c == 'r' && !Model.IsRecording()) {
            last_sec = 0;
            if (Model.StartRecording()) {
                Net::Server.setRecording(true);
                View.SetRecording(0);
                Serial.println("[REC] (serial) start ok");
            } else {
                View.SetError(LV_SYMBOL_WARNING " SD card error");
                Serial.println("[REC] (serial) start FAILED");
            }
        } else if (c == 's' && Model.IsRecording()) {
            Model.StopRecording();
            Net::Server.setRecording(false);
            Net::Server.requestNotify();
            View.SetSaved(Model.LastFilename(), last_sec);
            Serial.println("[REC] (serial) stop ok");
        }
    }

    // WiFi/reachability indicator — refresh whether idle or recording.
    int wifi = Net::Server.wifiConnected() ? 1 : 0;
    if (wifi != last_wifi) {
        last_wifi = wifi;
        View.SetWifi(wifi == 1);
    }

    if (!Model.IsRecording()) return;

    Model.WriteChunk();

    // If WriteChunk auto-rolled over to a new file (sustained mic failure
    // recovered), notify Headspace so the just-finalised file gets pulled +
    // transcribed; reset the displayed timer to track the new file from 0.
    if (Model.RolloverHappened()) {
        Net::Server.requestNotify();
        last_sec = 0;
    }

    View.SetLevel(Model.InputLevel());

    uint32_t sec = Model.RecordingSeconds();
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

    if (obj == instance->_root) {
        // Hidden gesture: swipe down (over the background/logo) opens the
        // factory hardware demo. Only when idle, so it can't cut a meeting.
        if (code == LV_EVENT_GESTURE) {
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
            if (dir == LV_DIR_BOTTOM && !instance->Model.IsRecording()) {
                instance->_Manager->Replace("Pages/HomeMenu");
            }
        }
        return;
    }

    if (code == LV_EVENT_CLICKED && obj == instance->View.ui.btn_record) {
        if (instance->Model.IsRecording()) {
            instance->Model.StopRecording();
            Net::Server.setRecording(false);  // re-open the HTTP server
            Net::Server.requestNotify();      // announce the new recording to Headspace
            instance->View.SetSaved(instance->Model.LastFilename(),
                                    instance->last_sec);
        } else {
            instance->last_sec = 0;
            if (instance->Model.StartRecording()) {
                Net::Server.setRecording(true);  // pause serving during capture
                instance->View.SetRecording(0);
            } else {
                instance->View.SetError(LV_SYMBOL_WARNING " SD card error");
            }
        }
    }
}
