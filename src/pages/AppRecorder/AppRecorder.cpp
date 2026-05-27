#include "AppRecorder.h"

using namespace Page;

AppRecorder::AppRecorder() : timer(nullptr), last_sec(0) {}

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
    if (!Model.IsRecording()) return;

    Model.WriteChunk();
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
            instance->View.SetSaved(instance->Model.LastFilename(),
                                    instance->last_sec);
        } else {
            instance->last_sec = 0;
            if (instance->Model.StartRecording()) {
                instance->View.SetRecording(0);
            } else {
                instance->View.SetError(LV_SYMBOL_WARNING " SD card error");
            }
        }
    }
}
