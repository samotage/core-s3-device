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

    // Upload indicator (left of WiFi) — shown only while a recording is being
    // pushed to Headspace. Hidden by default; toggled directly via SetUploading()
    // immediately before/after the blocking upload POST (the 5s refresh timer
    // can't paint it because the transfer freezes the loop).
    icon_upload_ = lv_label_create(root_);
    lv_obj_set_style_text_font(icon_upload_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(icon_upload_, COL_COPPER, 0);
    lv_label_set_text(icon_upload_, LV_SYMBOL_UPLOAD);
    lv_obj_align(icon_upload_, LV_ALIGN_RIGHT_MID, -34, 0);
    lv_obj_add_flag(icon_upload_, LV_OBJ_FLAG_HIDDEN);

    // This instance is now the live status bar SetUploading() should drive.
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
}

// Toggle the upload glyph on the live status bar and force an immediate
// render+flush. Called from Net::Server.loop() either side of the blocking
// upload POST — the loop's own LVGL refresh can't run during the transfer, so we
// paint synchronously here. Same task as LVGL (the Arduino main loop), so
// touching the object directly is safe; lv_refr_now() flushes it to the panel
// before the POST freezes the screen.
void StatusBar::SetUploading(bool on) {
    if (!s_active_ || !s_active_->root_ || !s_active_->icon_upload_) return;
    if (on) {
        lv_obj_clear_flag(s_active_->icon_upload_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_active_->icon_upload_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_refr_now(NULL);
}
