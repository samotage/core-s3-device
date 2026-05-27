#include "AppRecorderView.h"

using namespace Page;

// otageLabs brand palette (website/dark variant — see brand guidelines v1.1)
#define COL_BG       lv_color_hex(0x1A1A1A)  // site background (near-black)
#define COL_COPPER   lv_color_hex(0xC78C5C)  // primary accent
#define COL_COPPER_D lv_color_hex(0x9C6B3F)  // copper, pressed
#define COL_RED      lv_color_hex(0xC75C5C)  // alert red (recording)
#define COL_OFFWHITE lv_color_hex(0xEDE8E2)  // warm off-white (primary text)
#define COL_GREY     lv_color_hex(0xA8A39D)  // muted foreground (secondary text)
#define COL_SURFACE  lv_color_hex(0x2A2A2A)  // secondary surface (VU track)

LV_IMG_DECLARE(otagelabs_logo);

void AppRecorderView::Create(lv_obj_t* root) {
    ui.root = root;

    // Dark brand background.
    lv_obj_set_style_bg_color(root, COL_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    // otageLabs logo (head + wordmark), composited on the dark bg.
    ui.img_logo = lv_img_create(root);
    lv_img_set_src(ui.img_logo, &otagelabs_logo);
    lv_obj_align(ui.img_logo, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_clear_flag(ui.img_logo, LV_OBJ_FLAG_CLICKABLE);  // let gestures reach root

    // Big circular record button — the one control.
    ui.btn_record = lv_btn_create(root);
    lv_obj_set_size(ui.btn_record, 104, 104);
    lv_obj_align(ui.btn_record, LV_ALIGN_TOP_MID, 0, 102);
    lv_obj_set_style_radius(ui.btn_record, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui.btn_record, COL_COPPER, 0);
    lv_obj_set_style_bg_color(ui.btn_record, COL_COPPER_D, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui.btn_record, 0, 0);
    lv_obj_set_style_shadow_width(ui.btn_record, 0, 0);

    ui.label_btn = lv_label_create(ui.btn_record);
    lv_label_set_text(ui.label_btn, "REC");
    lv_obj_set_style_text_color(ui.label_btn, COL_BG, 0);
    lv_obj_set_style_text_font(ui.label_btn, &lv_font_montserrat_34, 0);
    lv_obj_center(ui.label_btn);

    // Input-level (VU) bar — only shown while recording.
    ui.bar_level = lv_bar_create(root);
    lv_obj_set_size(ui.bar_level, 200, 6);
    lv_obj_align(ui.bar_level, LV_ALIGN_TOP_MID, 0, 212);
    lv_obj_set_style_radius(ui.bar_level, 3, 0);
    lv_obj_set_style_bg_color(ui.bar_level, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.bar_level, COL_COPPER, LV_PART_INDICATOR);
    lv_bar_set_range(ui.bar_level, 0, 100);
    lv_bar_set_value(ui.bar_level, 0, LV_ANIM_OFF);
    lv_obj_add_flag(ui.bar_level, LV_OBJ_FLAG_HIDDEN);

    // WiFi/reachability indicator, top-right (clear of the centred logo).
    ui.label_wifi = lv_label_create(root);
    lv_label_set_text(ui.label_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(ui.label_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ui.label_wifi, COL_SURFACE, 0);  // dim until connected
    lv_obj_align(ui.label_wifi, LV_ALIGN_TOP_RIGHT, -6, 6);

    // Status line.
    ui.label_status = lv_label_create(root);
    lv_obj_set_style_text_color(ui.label_status, COL_GREY, 0);
    lv_obj_set_style_text_font(ui.label_status, &lv_font_montserrat_14, 0);
    lv_obj_align(ui.label_status, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(ui.label_status, "");
}

void AppRecorderView::Delete() {}

void AppRecorderView::SetIdle(bool sd_present, uint64_t free_mb) {
    lv_obj_set_style_bg_color(ui.btn_record, COL_COPPER, 0);
    lv_label_set_text(ui.label_btn, "REC");
    lv_obj_set_style_text_color(ui.label_btn, COL_BG, 0);
    lv_obj_add_flag(ui.bar_level, LV_OBJ_FLAG_HIDDEN);

    if (!sd_present) {
        lv_obj_set_style_text_color(ui.label_status, COL_RED, 0);
        lv_label_set_text(ui.label_status, "Insert SD card");
    } else {
        lv_obj_set_style_text_color(ui.label_status, COL_GREY, 0);
        if (free_mb >= 1024) {
            lv_label_set_text_fmt(ui.label_status, "Tap to record  -  %.1f GB free",
                                  free_mb / 1024.0);
        } else {
            lv_label_set_text_fmt(ui.label_status, "Tap to record  -  %llu MB free",
                                  free_mb);
        }
    }
}

void AppRecorderView::SetRecording(uint32_t seconds) {
    lv_obj_set_style_bg_color(ui.btn_record, COL_RED, 0);
    lv_obj_set_style_text_color(ui.label_btn, COL_OFFWHITE, 0);
    lv_label_set_text_fmt(ui.label_btn, "%02u:%02u", seconds / 60, seconds % 60);
    lv_obj_clear_flag(ui.bar_level, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(ui.label_status, COL_RED, 0);
    lv_label_set_text(ui.label_status, LV_SYMBOL_STOP "  Tap to stop");
}

void AppRecorderView::SetSaved(const char* filename, uint32_t seconds) {
    lv_obj_set_style_bg_color(ui.btn_record, COL_COPPER, 0);
    lv_label_set_text(ui.label_btn, "REC");
    lv_obj_set_style_text_color(ui.label_btn, COL_BG, 0);
    lv_obj_add_flag(ui.bar_level, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(ui.label_status, COL_COPPER, 0);
    const char* name = (filename && filename[0] == '/') ? filename + 1 : filename;
    lv_label_set_text_fmt(ui.label_status, LV_SYMBOL_OK " Saved %s  (%02u:%02u)",
                          name ? name : "", seconds / 60, seconds % 60);
}

void AppRecorderView::SetError(const char* msg) {
    lv_obj_set_style_text_color(ui.label_status, COL_RED, 0);
    lv_label_set_text(ui.label_status, msg);
}

void AppRecorderView::SetLevel(uint8_t level) {
    lv_bar_set_value(ui.bar_level, level, LV_ANIM_OFF);
}

void AppRecorderView::SetWifi(bool connected) {
    lv_obj_set_style_text_color(ui.label_wifi, connected ? COL_COPPER : COL_SURFACE, 0);
}
