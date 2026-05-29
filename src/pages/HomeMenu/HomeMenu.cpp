#include "HomeMenu.h"
#include "../AppRecorder/AppRecorderModel.h"
#include "../AppPower/AppPowerModel.h"

using namespace Page;

HomeMenu::HomeMenu() : timer(nullptr) {}
HomeMenu::~HomeMenu() {}

void HomeMenu::onCustomAttrConfig() {
    LV_LOG_USER(__func__);
}

void HomeMenu::onViewLoad() {
    LV_LOG_USER(__func__);
    View.Create(_root);

    AttachEvent(View.ui.btn_recorder, LV_EVENT_CLICKED);
    AttachEvent(View.ui.btn_files,    LV_EVENT_CLICKED);
    AttachEvent(View.ui.btn_settings, LV_EVENT_CLICKED);
    AttachEvent(View.ui.btn_sleep,    LV_EVENT_CLICKED);
    AttachEvent(View.ui.btn_poweroff, LV_EVENT_CLICKED);

    // FR25: hide Sleep when recording.
    bool recording = (g_app_recorder_model && g_app_recorder_model->IsRecording());
    View.SetSleepEnabled(!recording);
}

void HomeMenu::onViewDidLoad()        { LV_LOG_USER(__func__); }
void HomeMenu::onViewWillAppear() {
    LV_LOG_USER(__func__);
    timer = lv_timer_create(onTimerUpdate, 1000, this);
}
void HomeMenu::onViewDidAppear()      { LV_LOG_USER(__func__); }
void HomeMenu::onViewWillDisappear()  { LV_LOG_USER(__func__); }
void HomeMenu::onViewDidDisappear()   { LV_LOG_USER(__func__); lv_timer_del(timer); }
void HomeMenu::onViewUnload()         { LV_LOG_USER(__func__); View.Delete(); }
void HomeMenu::onViewDidUnload()      { LV_LOG_USER(__func__); }

void HomeMenu::AttachEvent(lv_obj_t* obj, lv_event_code_t code) {
    lv_obj_set_user_data(obj, this);
    lv_obj_add_event_cb(obj, onEvent, code, this);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void HomeMenu::Update() {
    // Refresh Sleep enable state — covers the case where recording started or
    // stopped while the menu was visible (FR25).
    bool recording = (g_app_recorder_model && g_app_recorder_model->IsRecording());
    View.SetSleepEnabled(!recording);
}

void HomeMenu::onTimerUpdate(lv_timer_t* timer) {
    HomeMenu* instance = (HomeMenu*)timer->user_data;
    instance->Update();
}

// Confirmation msgbox callbacks: identified by user_data, the action token.
enum class HmAction { None, PowerOff };
struct ConfirmCtx {
    HomeMenu* page;
    HmAction action;
};

static void onConfirmEvent(lv_event_t* e) {
    lv_obj_t* mbox = lv_event_get_current_target(e);
    ConfirmCtx* ctx = (ConfirmCtx*)lv_event_get_user_data(e);
    uint16_t btn_id = lv_msgbox_get_active_btn(mbox);
    bool yes = (btn_id == 0);  // "Yes" is the first button in our static map
    HmAction action = ctx->action;
    HomeMenu* page = ctx->page;
    delete ctx;
    lv_msgbox_close(mbox);

    if (yes && action == HmAction::PowerOff) {
        // FR29: if recording, finalise the WAV before shutdown.
        if (g_app_recorder_model && g_app_recorder_model->IsRecording()) {
            g_app_recorder_model->StopRecording();
        }
        AppPowerModel pm;
        pm.PowerOff();
        // PowerOff() does not return on hardware.
        (void)page;
    }
}

static void showConfirm(HomeMenu* page, const char* title, HmAction action) {
    static const char* btns[] = {"Yes", "No", ""};
    lv_obj_t* mbox = lv_msgbox_create(NULL, "Confirm", title, btns, true);
    ConfirmCtx* ctx = new ConfirmCtx{page, action};
    lv_obj_add_event_cb(mbox, onConfirmEvent, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_center(mbox);
}

void HomeMenu::onEvent(lv_event_t* event) {
    HomeMenu* instance = (HomeMenu*)lv_event_get_user_data(event);
    LV_ASSERT_NULL(instance);

    lv_obj_t* obj        = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED) return;

    if (obj == instance->View.ui.btn_recorder) {
        instance->_Manager->Replace("Pages/AppRecorder");
    } else if (obj == instance->View.ui.btn_files) {
        instance->_Manager->Replace("Pages/AppFiles");
    } else if (obj == instance->View.ui.btn_settings) {
        instance->_Manager->Replace("Pages/AppSettings");
    } else if (obj == instance->View.ui.btn_sleep) {
        // FR25: only allow when not recording (UI also hides it then).
        if (g_app_recorder_model && g_app_recorder_model->IsRecording()) return;
        AppPowerModel pm;
        pm.DeepSleep();
    } else if (obj == instance->View.ui.btn_poweroff) {
        showConfirm(instance, "Power off device?", HmAction::PowerOff);
    }
}
