#include "AppRecorderView.h"

using namespace Page;

#define COL_BG       lv_color_hex(0x1A1A1A)
#define COL_COPPER   lv_color_hex(0xC78C5C)
#define COL_COPPER_D lv_color_hex(0x9C6B3F)
#define COL_RED      lv_color_hex(0xC75C5C)
#define COL_OFFWHITE lv_color_hex(0xEDE8E2)
#define COL_GREY     lv_color_hex(0xA8A39D)
#define COL_SURFACE  lv_color_hex(0x2A2A2A)

LV_IMG_DECLARE(otagelabs_logo);

void AppRecorderView::Create(lv_obj_t* root) {
    ui.root = root;

    lv_obj_set_style_bg_color(root, COL_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    status_bar.Attach(root);
    status_bar.SetOnRecorderPage(true);  // suppresses red dot here (FR5)

    // Hero: otageLabs logo (FR6). 140x94 asset; sized as the dominant element.
    ui.img_logo = lv_img_create(root);
    lv_img_set_src(ui.img_logo, &otagelabs_logo);
    lv_obj_align(ui.img_logo, LV_ALIGN_TOP_MID, 0, StatusBar::HEIGHT_PX + 14);
    lv_obj_clear_flag(ui.img_logo, LV_OBJ_FLAG_CLICKABLE);

    // Timer (recording only) — positioned between logo and record button.
    ui.label_timer = lv_label_create(root);
    lv_obj_set_style_text_color(ui.label_timer, COL_OFFWHITE, 0);
    lv_obj_set_style_text_font(ui.label_timer, &lv_font_montserrat_22, 0);
    lv_obj_align(ui.label_timer, LV_ALIGN_TOP_MID, 0, StatusBar::HEIGHT_PX + 116);
    lv_label_set_text(ui.label_timer, "");
    lv_obj_add_flag(ui.label_timer, LV_OBJ_FLAG_HIDDEN);

    // VU bar (recording only).
    ui.bar_level = lv_bar_create(root);
    lv_obj_set_size(ui.bar_level, 200, 6);
    lv_obj_align(ui.bar_level, LV_ALIGN_TOP_MID, 0, StatusBar::HEIGHT_PX + 144);
    lv_obj_set_style_radius(ui.bar_level, 3, 0);
    lv_obj_set_style_bg_color(ui.bar_level, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.bar_level, COL_COPPER, LV_PART_INDICATOR);
    lv_bar_set_range(ui.bar_level, 0, 100);
    lv_bar_set_value(ui.bar_level, 0, LV_ANIM_OFF);
    lv_obj_add_flag(ui.bar_level, LV_OBJ_FLAG_HIDDEN);

    // Pill-shaped record button (FR7). Idle: copper "Record". Recording: red "Stop".
    ui.btn_record = lv_btn_create(root);
    lv_obj_set_size(ui.btn_record, 180, 48);
    lv_obj_align(ui.btn_record, LV_ALIGN_TOP_MID, 0, StatusBar::HEIGHT_PX + 158);
    lv_obj_set_style_radius(ui.btn_record, 24, 0);  // pill
    lv_obj_set_style_bg_color(ui.btn_record, COL_COPPER, 0);
    lv_obj_set_style_bg_color(ui.btn_record, COL_COPPER_D, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui.btn_record, 0, 0);
    lv_obj_set_style_shadow_width(ui.btn_record, 0, 0);

    ui.label_btn = lv_label_create(ui.btn_record);
    lv_label_set_text(ui.label_btn, "Record");
    lv_obj_set_style_text_color(ui.label_btn, COL_BG, 0);
    lv_obj_set_style_text_font(ui.label_btn, &lv_font_montserrat_22, 0);
    lv_obj_center(ui.label_btn);

    // Storage-full message + "Go to Files" jump (FR33-FR34). Hidden by default.
    ui.label_status = lv_label_create(root);
    lv_obj_set_style_text_color(ui.label_status, COL_GREY, 0);
    lv_obj_set_style_text_font(ui.label_status, &lv_font_montserrat_14, 0);
    lv_obj_align(ui.label_status, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_label_set_text(ui.label_status, "");

    ui.btn_to_files = lv_btn_create(root);
    lv_obj_set_size(ui.btn_to_files, 140, 28);
    lv_obj_align(ui.btn_to_files, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_radius(ui.btn_to_files, 6, 0);
    lv_obj_set_style_bg_color(ui.btn_to_files, COL_SURFACE, 0);
    {
        lv_obj_t* l = lv_label_create(ui.btn_to_files);
        lv_label_set_text(l, "Open Files");
        lv_obj_set_style_text_color(l, COL_OFFWHITE, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_center(l);
    }
    lv_obj_add_flag(ui.btn_to_files, LV_OBJ_FLAG_HIDDEN);

    // Menu (back) button (FR12) — bottom-left, doesn't crowd the hero.
    ui.btn_menu = lv_btn_create(root);
    lv_obj_set_size(ui.btn_menu, 70, 28);
    lv_obj_align(ui.btn_menu, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_set_style_radius(ui.btn_menu, 6, 0);
    lv_obj_set_style_bg_color(ui.btn_menu, COL_SURFACE, 0);
    lv_obj_set_style_border_width(ui.btn_menu, 0, 0);
    {
        lv_obj_t* l = lv_label_create(ui.btn_menu);
        lv_label_set_text(l, LV_SYMBOL_LEFT " Menu");
        lv_obj_set_style_text_color(l, COL_OFFWHITE, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_center(l);
    }
}

void AppRecorderView::Delete() {
    status_bar.Detach();
}

void AppRecorderView::SetIdle() {
    lv_obj_set_style_bg_color(ui.btn_record, COL_COPPER, 0);
    lv_label_set_text(ui.label_btn, "Record");
    lv_obj_set_style_text_color(ui.label_btn, COL_BG, 0);
    lv_obj_clear_state(ui.btn_record, LV_STATE_DISABLED);
    lv_obj_add_flag(ui.bar_level,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.label_timer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.btn_to_files,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui.label_status, "");
}

void AppRecorderView::SetRecording(uint32_t seconds) {
    lv_obj_set_style_bg_color(ui.btn_record, COL_RED, 0);
    lv_label_set_text(ui.label_btn, "Stop");
    lv_obj_set_style_text_color(ui.label_btn, COL_OFFWHITE, 0);
    lv_obj_clear_flag(ui.bar_level,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui.label_timer, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(ui.label_timer, "%02u:%02u", seconds / 60, seconds % 60);
    lv_label_set_text(ui.label_status, "");
}

void AppRecorderView::SetSaved(const char* filename, uint32_t seconds) {
    SetIdle();
    lv_obj_set_style_text_color(ui.label_status, COL_COPPER, 0);
    const char* name = (filename && filename[0] == '/') ? filename + 1 : filename;
    lv_label_set_text_fmt(ui.label_status, LV_SYMBOL_OK " Saved %s (%02u:%02u)",
                          name ? name : "", seconds / 60, seconds % 60);
}

void AppRecorderView::SetError(const char* msg) {
    lv_obj_set_style_text_color(ui.label_status, COL_RED, 0);
    lv_label_set_text(ui.label_status, msg);
}

void AppRecorderView::SetStorageFull() {
    // FR33-FR34: disable record button + offer navigation to Files.
    lv_obj_add_state(ui.btn_record, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(ui.btn_record, COL_SURFACE, 0);
    lv_obj_set_style_text_color(ui.label_status, COL_RED, 0);
    lv_label_set_text(ui.label_status, "Storage full — delete recordings to continue.");
    lv_obj_clear_flag(ui.btn_to_files, LV_OBJ_FLAG_HIDDEN);
}

void AppRecorderView::SetLevel(uint8_t level) {
    lv_bar_set_value(ui.bar_level, level, LV_ANIM_OFF);
}
