#include <stdarg.h>
#include <stdio.h>
#include "AppMicView.h"

using namespace Page;

void AppMicView::Create(lv_obj_t* root) {
    ui.img_bg = lv_img_create(root);
    lv_img_set_src(ui.img_bg, ResourcePool::GetImage("app_mic"));

    ui.imgbtn_home = lv_imgbtn_create(root);
    lv_obj_set_size(ui.imgbtn_home, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_imgbtn_set_src(ui.imgbtn_home, LV_IMGBTN_STATE_RELEASED, NULL, ResourcePool::GetImage("home_r"), NULL);
    lv_imgbtn_set_src(ui.imgbtn_home, LV_IMGBTN_STATE_PRESSED, NULL, ResourcePool::GetImage("home_p"), NULL);
    lv_obj_align(ui.imgbtn_home, LV_ALIGN_TOP_LEFT, 0, 0);

    ui.imgbtn_next = lv_imgbtn_create(root);
    lv_obj_set_size(ui.imgbtn_next, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_imgbtn_set_src(ui.imgbtn_next, LV_IMGBTN_STATE_RELEASED, NULL, ResourcePool::GetImage("next_r"), NULL);
    lv_imgbtn_set_src(ui.imgbtn_next, LV_IMGBTN_STATE_PRESSED, NULL, ResourcePool::GetImage("next_p"), NULL);
    lv_obj_align(ui.imgbtn_next, LV_ALIGN_TOP_RIGHT, 5, 0);

    ui.btn_top_center = lv_btn_create(root);
    lv_obj_remove_style_all(ui.btn_top_center);
    lv_obj_set_size(ui.btn_top_center, 170, 68);
    lv_obj_align(ui.btn_top_center, LV_ALIGN_TOP_MID, 0, 0);

    // Record button
    ui.btn_record = lv_btn_create(root);
    lv_obj_set_size(ui.btn_record, 120, 36);
    lv_obj_align(ui.btn_record, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(ui.btn_record, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_bg_color(ui.btn_record, lv_color_hex(0x990000), LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui.btn_record, 8, 0);
    lv_obj_set_style_shadow_width(ui.btn_record, 0, 0);

    ui.label_record = lv_label_create(ui.btn_record);
    lv_label_set_text(ui.label_record, LV_SYMBOL_AUDIO " REC");
    lv_obj_set_style_text_color(ui.label_record, lv_color_white(), 0);
    lv_obj_set_style_text_font(ui.label_record, &lv_font_montserrat_14, 0);
    lv_obj_center(ui.label_record);

    // Status label
    ui.label_status = lv_label_create(root);
    lv_label_set_text(ui.label_status, "");
    lv_obj_set_style_text_color(ui.label_status, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(ui.label_status, &lv_font_montserrat_14, 0);
    lv_obj_align(ui.label_status, LV_ALIGN_BOTTOM_MID, 0, -50);
}

void AppMicView::Delete() {
}

void AppMicView::SetRecording(bool is_recording, uint32_t seconds, const char* filename) {
    if (is_recording) {
        lv_obj_set_style_bg_color(ui.btn_record, lv_color_hex(0x006600), 0);
        lv_label_set_text_fmt(ui.label_record, LV_SYMBOL_STOP " %02d:%02d",
            seconds / 60, seconds % 60);
        if (filename) {
            lv_label_set_text(ui.label_status, filename + 1);
        }
    } else {
        lv_obj_set_style_bg_color(ui.btn_record, lv_color_hex(0xCC0000), 0);
        lv_label_set_text(ui.label_record, LV_SYMBOL_AUDIO " REC");
        if (filename && filename[0]) {
            lv_label_set_text_fmt(ui.label_status, "Saved: %s", filename + 1);
        } else {
            lv_label_set_text(ui.label_status, "");
        }
    }
}
