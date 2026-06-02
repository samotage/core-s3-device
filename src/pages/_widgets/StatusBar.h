#ifndef __STATUS_BAR_H
#define __STATUS_BAR_H

#include "../Page.h"
#include "../AppPower/AppPowerModel.h"

namespace Page {

// Shared status bar widget. Attach() spawns a top-bar overlay on any page.
// One AppPowerModel sampler is shared across instances via a class-static
// pointer; refresh tick reads it on a 5s lv_timer (PRD NFR2 — no impact on
// mic DMA timing because we run from the LVGL timer, not the mic loop).
class StatusBar {
   public:
    void Attach(lv_obj_t* parent);
    void Detach();

    // Tell the status bar whether the current page is the recorder (so the
    // recording dot only shows when navigated AWAY from the recorder screen,
    // per FR5). Default: false.
    void SetOnRecorderPage(bool on_recorder);

    // Show/hide the upload glyph on the currently-mounted status bar. Static
    // because it's driven from Net::Server.loop() (the upload path), not from a
    // page. The upload POST blocks the loop with the screen frozen, so this
    // paints + flushes the indicator synchronously rather than waiting for the
    // 5s refresh timer. Safe to call from the main loop — same task as LVGL.
    static void SetUploading(bool on);

    static constexpr int HEIGHT_PX = 22;

   private:
    static void onTimer(lv_timer_t* t);
    void Refresh();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* label_battery_ = nullptr;
    lv_obj_t* icon_charging_ = nullptr;
    lv_obj_t* icon_wifi_     = nullptr;
    lv_obj_t* icon_upload_   = nullptr;
    lv_obj_t* icon_rec_      = nullptr;
    lv_timer_t* timer_       = nullptr;
    AppPowerModel power_;  // local sampler (status bar is the consumer here)
    bool on_recorder_page_ = false;

    // Points at the currently-attached status bar so the static SetUploading()
    // can reach the live instance. Set in Attach(), cleared in Detach().
    static StatusBar* s_active_;
};

}  // namespace Page

#endif
