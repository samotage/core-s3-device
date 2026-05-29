#ifndef __APPSETTINGS_H
#define __APPSETTINGS_H

#include "../Page.h"
#include "../_widgets/StatusBar.h"

namespace Page {

// App-level brightness setting. Persists for the session — survives page nav.
// Used by main.cpp on boot via GetCurrentBrightness().
extern uint8_t g_app_brightness;
uint8_t GetCurrentBrightness();

class AppSettings : public PageBase {
   public:
    AppSettings();
    virtual ~AppSettings();

    virtual void onCustomAttrConfig();
    virtual void onViewLoad();
    virtual void onViewDidLoad();
    virtual void onViewWillAppear();
    virtual void onViewDidAppear();
    virtual void onViewWillDisappear();
    virtual void onViewDidDisappear();
    virtual void onViewUnload();
    virtual void onViewDidUnload();

   public:
    struct {
        lv_obj_t* root;
        lv_obj_t* label_brightness;
        lv_obj_t* slider_brightness;
        lv_obj_t* btn_menu;
    } ui;
    StatusBar status_bar;

   private:
    static void onSlider(lv_event_t* e);
    static void onMenu(lv_event_t* e);
    void Build();
};

}  // namespace Page

#endif
