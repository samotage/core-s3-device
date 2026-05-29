#include "AppFiles.h"

using namespace Page;

AppFiles::AppFiles() {}
AppFiles::~AppFiles() {}

void AppFiles::onCustomAttrConfig() { LV_LOG_USER(__func__); }

void AppFiles::onViewLoad() {
    LV_LOG_USER(__func__);
    View.Create(_root);
    lv_obj_set_user_data(View.ui.btn_delete10, this);
    lv_obj_add_event_cb(View.ui.btn_delete10, onEvent, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(View.ui.btn_menu, this);
    lv_obj_add_event_cb(View.ui.btn_menu, onEvent, LV_EVENT_CLICKED, this);
}

void AppFiles::onViewDidLoad()        { LV_LOG_USER(__func__); }
void AppFiles::onViewWillAppear()     { LV_LOG_USER(__func__); RefreshStats(); }
void AppFiles::onViewDidAppear()      { LV_LOG_USER(__func__); }
void AppFiles::onViewWillDisappear()  { LV_LOG_USER(__func__); }
void AppFiles::onViewDidDisappear()   { LV_LOG_USER(__func__); }
void AppFiles::onViewUnload()         { LV_LOG_USER(__func__); View.Delete(); }
void AppFiles::onViewDidUnload()      { LV_LOG_USER(__func__); }

void AppFiles::RefreshStats() {
    View.SetBusy(true);
    lv_refr_now(NULL);  // paint spinner before we block on SD walk
    FilesStats s;
    if (Model.CollectStats(s)) {
        View.SetStats(s);
    }
    View.SetBusy(false);
}

struct DelCtx { AppFiles* page; };
static void onConfirmDelete(lv_event_t* e) {
    lv_obj_t* mbox = lv_event_get_current_target(e);
    DelCtx* ctx = (DelCtx*)lv_event_get_user_data(e);
    uint16_t btn_id = lv_msgbox_get_active_btn(mbox);
    bool yes = (btn_id == 0);
    AppFiles* page = ctx->page;
    delete ctx;
    lv_msgbox_close(mbox);
    if (yes && page) {
        page->View.SetBusy(true);
        lv_refr_now(NULL);
        page->Model.DeleteOldestRecordings(10);
        page->RefreshStats();
    }
}

void AppFiles::onEvent(lv_event_t* event) {
    AppFiles* instance = (AppFiles*)lv_event_get_user_data(event);
    LV_ASSERT_NULL(instance);
    lv_obj_t* obj        = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED) return;

    if (obj == instance->View.ui.btn_menu) {
        instance->_Manager->Replace("Pages/HomeMenu");
        return;
    }
    if (obj == instance->View.ui.btn_delete10) {
        static const char* btns[] = {"Yes", "No", ""};
        lv_obj_t* mbox = lv_msgbox_create(NULL, "Confirm",
                                          "Delete 10 oldest recordings?", btns, true);
        DelCtx* ctx = new DelCtx{instance};
        lv_obj_add_event_cb(mbox, onConfirmDelete, LV_EVENT_VALUE_CHANGED, ctx);
        lv_obj_center(mbox);
    }
}
