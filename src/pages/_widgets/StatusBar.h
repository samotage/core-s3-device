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

    // Repaint the upload counter on the currently-mounted status bar from the
    // live Net::Server.uploadStatus() snapshot, and force an immediate flush.
    // Static because it's driven from Net::Server.loop() (the upload path), not a
    // page. The upload POST blocks the loop with the screen frozen, so the 5s
    // refresh timer can't paint per-file progress — the upload path pokes us
    // synchronously around each transfer. Pass schedule_clear=true when entering
    // a transient (✓ done / ⚠ failed) state so a one-shot timer repaints it away
    // after the hold window. Safe from the main loop — same task as LVGL.
    static void PokeUpload(bool schedule_clear = false);

    static constexpr int HEIGHT_PX = 22;

   private:
    static void onTimer(lv_timer_t* t);
    static void onTransientEnd(lv_timer_t* t);  // clears the ✓/⚠ hold
    void Refresh();
    void ApplyUploadLabel();  // render upload counter from Net::Server snapshot

    lv_obj_t* root_ = nullptr;
    lv_obj_t* label_battery_ = nullptr;
    lv_obj_t* icon_charging_ = nullptr;
    lv_obj_t* icon_wifi_     = nullptr;
    lv_obj_t* label_upload_  = nullptr;  // "⬆ N" / "⬆ i/N" / "✓" / "⚠ N"
    lv_obj_t* icon_rec_      = nullptr;
    lv_timer_t* timer_       = nullptr;
    AppPowerModel power_;  // local sampler (status bar is the consumer here)
    bool on_recorder_page_ = false;

    // Points at the currently-attached status bar so the static PokeUpload()
    // can reach the live instance. Set in Attach(), cleared in Detach().
    static StatusBar* s_active_;
};

}  // namespace Page

#endif
