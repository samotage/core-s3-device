#ifndef __APPRECORDER_PRESENTER_H
#define __APPRECORDER_PRESENTER_H

#include "AppRecorderView.h"
#include "AppRecorderModel.h"
#include "M5Unified.h"

namespace Page {

class AppRecorder : public PageBase {
   public:
    AppRecorder();
    virtual ~AppRecorder();

    virtual void onCustomAttrConfig();
    virtual void onViewLoad();
    virtual void onViewDidLoad();
    virtual void onViewWillAppear();
    virtual void onViewDidAppear();
    virtual void onViewWillDisappear();
    virtual void onViewDidDisappear();
    virtual void onViewUnload();
    virtual void onViewDidUnload();

   private:
    void Update();
    void ShowIdle();
    void AttachEvent(lv_obj_t* obj) { AttachEvent(obj, LV_EVENT_ALL); }
    void AttachEvent(lv_obj_t* obj, lv_event_code_t code);
    static void onTimerUpdate(lv_timer_t* timer);
    static void onEvent(lv_event_t* event);

   private:
    AppRecorderView View;
    AppRecorderModel Model;
    lv_timer_t* timer;
    uint32_t last_sec;
    int last_wifi;  // -1 unknown, 0 down, 1 up — only repaint on change
};

}  // namespace Page

#endif
