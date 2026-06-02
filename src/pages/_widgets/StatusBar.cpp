#include "StatusBar.h"
#include "../AppRecorder/AppRecorderModel.h"
#include "../../net/RecorderServer.h"

using namespace Page;

StatusBar* StatusBar::s_active_ = nullptr;

#define COL_BG       lv_color_hex(0x1A1A1A)
#define COL_COPPER   lv_color_hex(0xC78C5C)
#define COL_RED      lv_color_hex(0xC75C5C)
#define COL_OFFWHITE lv_color_hex(0xEDE8E2)
#define COL_GREY     lv_color_hex(0xA8A39D)
#define COL_SURFACE  lv_color_hex(0x2A2A2A)

void StatusBar::Attach(lv_obj_t* parent) {
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_HOR_RES, HEIGHT_PX);
    lv_obj_align(root_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(root_, COL_BG, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root_, 2, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_CLICKABLE);

    // Battery % (left)
    label_battery_ = lv_label_create(root_);
    lv_obj_set_style_text_font(label_battery_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_battery_, COL_OFFWHITE, 0);
    lv_label_set_text(label_battery_, "--%");
    lv_obj_align(label_battery_, LV_ALIGN_LEFT_MID, 4, 0);

    // Charging bolt (next to battery)
    icon_charging_ = lv_label_create(root_);
    lv_obj_set_style_text_font(icon_charging_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(icon_charging_, COL_COPPER, 0);
    lv_label_set_text(icon_charging_, LV_SYMBOL_CHARGE);
    lv_obj_align(icon_charging_, LV_ALIGN_LEFT_MID, 48, 0);
    lv_obj_add_flag(icon_charging_, LV_OBJ_FLAG_HIDDEN);

    // Recording dot (centre-right)
    icon_rec_ = lv_label_create(root_);
    lv_obj_set_style_text_font(icon_rec_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(icon_rec_, COL_RED, 0);
    lv_label_set_text(icon_rec_, LV_SYMBOL_PLAY " REC");
    lv_obj_align(icon_rec_, LV_ALIGN_RIGHT_MID, -50, 0);
    lv_obj_add_flag(icon_rec_, LV_OBJ_FLAG_HIDDEN);

    // WiFi indicator (right) — sized for glance via Montserrat 20 (vs old 14).
    icon_wifi_ = lv_label_create(root_);
    lv_obj_set_style_text_font(icon_wifi_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon_wifi_, COL_SURFACE, 0);  // dim until connected
    lv_label_set_text(icon_wifi_, LV_SYMBOL_WIFI);
    lv_obj_align(icon_wifi_, LV_ALIGN_RIGHT_MID, -6, 0);

    // Upload counter (left of WiFi). Tracks recordings not yet pushed to
    // Headspace: "⬆ N" backlog (copper when sendable, grey when waiting on WiFi),
    // "⬆ i/N" per-file progress during a push, transient "✓" on all-sent and
    // "⚠ N" on a failed cycle. Grows leftward from its right anchor so it never
    // collides with the WiFi glyph. Driven by ApplyUploadLabel() from both the
    // 5s timer (idle) and PokeUpload() (synchronous, around the blocking POST).
    label_upload_ = lv_label_create(root_);
    lv_obj_set_style_text_font(label_upload_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_upload_, COL_COPPER, 0);
    lv_label_set_text(label_upload_, "");
    lv_obj_align(label_upload_, LV_ALIGN_RIGHT_MID, -32, 0);
    lv_obj_add_flag(label_upload_, LV_OBJ_FLAG_HIDDEN);

    // This instance is now the live status bar PokeUpload() should drive.
    s_active_ = this;

    // Initial paint + ongoing refresh.
    Refresh();
    timer_ = lv_timer_create(StatusBar::onTimer, STATUS_BAR_REFRESH_MS, this);
}

void StatusBar::Detach() {
    if (s_active_ == this) s_active_ = nullptr;
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    if (root_) {
        lv_obj_del(root_);
        root_ = nullptr;
    }
}

void StatusBar::SetOnRecorderPage(bool on_recorder) {
    on_recorder_page_ = on_recorder;
    if (root_) Refresh();
}

void StatusBar::onTimer(lv_timer_t* t) {
    StatusBar* sb = (StatusBar*)t->user_data;
    sb->Refresh();
}

void StatusBar::Refresh() {
    if (!root_) return;

    // Battery percentage via LiPo curve (FR2).
    uint16_t mv = power_.SampleBatteryMv();
    uint8_t pct = (mv == 0) ? 0 : AppPowerModel::BatteryPercentFromMv(mv);
    if (mv == 0) {
        lv_label_set_text(label_battery_, "--%");
    } else {
        lv_label_set_text_fmt(label_battery_, "%u%%", pct);
    }

    // Charging icon (FR3): show when AXP reports charging.
    uint8_t chg = power_.AxpBatIsCharging();
    if (chg == 1) {
        lv_obj_clear_flag(icon_charging_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(icon_charging_, LV_OBJ_FLAG_HIDDEN);
    }

    bool recording = (g_app_recorder_model && g_app_recorder_model->IsRecording());

    // WiFi indicator (FR4). During recording the radio is deliberately OFF
    // (yielded to I2S capture), so show a clearly-visible grey reflecting the
    // real "off" state rather than the near-invisible disconnected surface dim.
    // Otherwise colour by reachability: copper = connected, surface = down.
    if (recording) {
        lv_obj_set_style_text_color(icon_wifi_, COL_GREY, 0);
    } else {
        bool wifi = Net::Server.wifiConnected();
        lv_obj_set_style_text_color(icon_wifi_, wifi ? COL_COPPER : COL_SURFACE, 0);
    }

    // Recording dot (FR5) — only when recording AND not on recorder screen.
    if (recording && !on_recorder_page_) {
        lv_obj_clear_flag(icon_rec_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(icon_rec_, LV_OBJ_FLAG_HIDDEN);
    }

    // Upload counter — same render path as the synchronous PokeUpload().
    ApplyUploadLabel();
}

// Render the upload counter from the live Net::Server snapshot. Single source of
// truth so the 5s timer and the synchronous upload-path pokes never disagree.
//   state 1 sending  -> "⬆ i/N" copper
//   state 2 done     -> "✓"     copper (transient)
//   state 3 failed   -> "⚠ N"   red   (transient)
//   idle, pending>0  -> "⬆ N"   copper if sendable now, grey if waiting on WiFi
//   idle, pending==0 -> hidden
void StatusBar::ApplyUploadLabel() {
    if (!label_upload_) return;
    Net::UploadStatus st = Net::Server.uploadStatus();

    if (st.state == 1) {  // sending file i of N
        lv_label_set_text_fmt(label_upload_, LV_SYMBOL_UPLOAD " %d/%d", st.index, st.total);
        lv_obj_set_style_text_color(label_upload_, COL_COPPER, 0);
        lv_obj_clear_flag(label_upload_, LV_OBJ_FLAG_HIDDEN);
    } else if (st.state == 2) {  // all sent (transient ✓)
        lv_label_set_text(label_upload_, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(label_upload_, COL_COPPER, 0);
        lv_obj_clear_flag(label_upload_, LV_OBJ_FLAG_HIDDEN);
    } else if (st.state == 3) {  // cycle failed (transient ⚠ N)
        lv_label_set_text_fmt(label_upload_, LV_SYMBOL_WARNING " %d", st.pending);
        lv_obj_set_style_text_color(label_upload_, COL_RED, 0);
        lv_obj_clear_flag(label_upload_, LV_OBJ_FLAG_HIDDEN);
    } else if (st.pending > 0) {  // idle backlog
        lv_label_set_text_fmt(label_upload_, LV_SYMBOL_UPLOAD " %d", st.pending);
        // Copper when it can send right now (WiFi up, not recording); grey when
        // the backlog is just waiting (recording, or no WiFi yet).
        bool recording = (g_app_recorder_model && g_app_recorder_model->IsRecording());
        bool can_send  = Net::Server.wifiConnected() && !recording;
        lv_obj_set_style_text_color(label_upload_, can_send ? COL_COPPER : COL_GREY, 0);
        lv_obj_clear_flag(label_upload_, LV_OBJ_FLAG_HIDDEN);
    } else {  // nothing pending
        lv_obj_add_flag(label_upload_, LV_OBJ_FLAG_HIDDEN);
    }
}

// Repaint the upload counter synchronously and flush it to the panel. Called from
// Net::Server's upload path around the blocking POST (the loop's own refresh
// can't run during a transfer). Same task as LVGL, so touching the object is
// safe. schedule_clear arms a one-shot timer to repaint the transient ✓/⚠ away
// once its hold window (tracked in Net::Server) has elapsed.
void StatusBar::PokeUpload(bool schedule_clear) {
    if (!s_active_ || !s_active_->root_) return;
    s_active_->ApplyUploadLabel();
    lv_refr_now(NULL);
    if (schedule_clear) {
        // Repaint shortly after the 2s hold expires; one-shot, auto-deletes.
        lv_timer_t* t = lv_timer_create(StatusBar::onTransientEnd, 2200, nullptr);
        lv_timer_set_repeat_count(t, 1);
    }
}

void StatusBar::onTransientEnd(lv_timer_t* /*t*/) {
    // The ✓/⚠ hold window has passed; uploadStatus() now reports idle, so a plain
    // repaint drops back to the backlog count (or hides). No flush needed beyond
    // the normal LVGL refresh, but do it now so it's crisp.
    if (!s_active_ || !s_active_->root_) return;
    s_active_->ApplyUploadLabel();
}
