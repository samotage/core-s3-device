#include "AppFilesView.h"

using namespace Page;

#define COL_BG       lv_color_hex(0x1A1A1A)
#define COL_COPPER   lv_color_hex(0xC78C5C)
#define COL_COPPER_D lv_color_hex(0x9C6B3F)
#define COL_RED      lv_color_hex(0xC75C5C)
#define COL_OFFWHITE lv_color_hex(0xEDE8E2)
#define COL_GREY     lv_color_hex(0xA8A39D)
#define COL_SURFACE  lv_color_hex(0x2A2A2A)

void AppFilesView::Create(lv_obj_t* root) {
    ui.root = root;
    lv_obj_set_style_bg_color(root, COL_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    status_bar.Attach(root);

    const lv_coord_t kY0 = StatusBar::HEIGHT_PX + 16;

    ui.label_files = lv_label_create(root);
    lv_obj_set_style_text_color(ui.label_files, COL_OFFWHITE, 0);
    lv_obj_set_style_text_font(ui.label_files, &lv_font_montserrat_16, 0);
    lv_obj_align(ui.label_files, LV_ALIGN_TOP_LEFT, 18, kY0);
    lv_label_set_text(ui.label_files, "Files: --");

    ui.label_storage = lv_label_create(root);
    lv_obj_set_style_text_color(ui.label_storage, COL_OFFWHITE, 0);
    lv_obj_set_style_text_font(ui.label_storage, &lv_font_montserrat_16, 0);
    lv_obj_align(ui.label_storage, LV_ALIGN_TOP_LEFT, 18, kY0 + 30);
    lv_label_set_text(ui.label_storage, "Storage: --");

    ui.label_est = lv_label_create(root);
    lv_obj_set_style_text_color(ui.label_est, COL_GREY, 0);
    lv_obj_set_style_text_font(ui.label_est, &lv_font_montserrat_16, 0);
    lv_obj_align(ui.label_est, LV_ALIGN_TOP_LEFT, 18, kY0 + 60);
    lv_label_set_text(ui.label_est, "Est. time: --");

    ui.btn_delete10 = lv_btn_create(root);
    lv_obj_set_size(ui.btn_delete10, 200, 36);
    lv_obj_align(ui.btn_delete10, LV_ALIGN_TOP_MID, 0, kY0 + 100);
    lv_obj_set_style_radius(ui.btn_delete10, 6, 0);
    lv_obj_set_style_bg_color(ui.btn_delete10, COL_COPPER, 0);
    lv_obj_set_style_bg_color(ui.btn_delete10, COL_COPPER_D, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui.btn_delete10, 0, 0);
    {
        lv_obj_t* l = lv_label_create(ui.btn_delete10);
        lv_label_set_text(l, "Delete oldest 10");
        lv_obj_set_style_text_color(l, COL_BG, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_center(l);
    }

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

    ui.spinner = lv_spinner_create(root, 1000, 60);
    lv_obj_set_size(ui.spinner, 40, 40);
    lv_obj_align(ui.spinner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
}

void AppFilesView::Delete() {
    status_bar.Detach();
}

void AppFilesView::SetStats(const FilesStats& s) {
    lv_label_set_text_fmt(ui.label_files, "Files: %u", (unsigned)s.file_count);
    double used_gb  = (double)s.used_bytes  / (1024.0 * 1024.0 * 1024.0);
    double total_gb = (double)s.total_bytes / (1024.0 * 1024.0 * 1024.0);
    lv_label_set_text_fmt(ui.label_storage, "Storage: %.1f / %.1f GB",
                          used_gb, total_gb);
    if (s.est_minutes_left >= 60) {
        lv_label_set_text_fmt(ui.label_est, "Est. time: ~%u hr",
                              (unsigned)(s.est_minutes_left / 60));
    } else {
        lv_label_set_text_fmt(ui.label_est, "Est. time: ~%u min",
                              (unsigned)s.est_minutes_left);
    }
}

void AppFilesView::SetBusy(bool busy) {
    if (busy) {
        lv_obj_clear_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(ui.btn_delete10, LV_STATE_DISABLED);
    } else {
        lv_obj_add_flag(ui.spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(ui.btn_delete10, LV_STATE_DISABLED);
    }
}
