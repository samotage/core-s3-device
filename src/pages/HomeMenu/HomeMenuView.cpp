#include <stdarg.h>
#include <stdio.h>
#include "HomeMenuView.h"

using namespace Page;

#define COL_BG       lv_color_hex(0x1A1A1A)
#define COL_COPPER   lv_color_hex(0xC78C5C)
#define COL_COPPER_D lv_color_hex(0x9C6B3F)
#define COL_RED      lv_color_hex(0xC75C5C)
#define COL_OFFWHITE lv_color_hex(0xEDE8E2)
#define COL_GREY     lv_color_hex(0xA8A39D)
#define COL_SURFACE  lv_color_hex(0x2A2A2A)

static lv_obj_t* makeMenuButton(lv_obj_t* parent, const char* label, lv_coord_t y) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 200, 32);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, COL_SURFACE, 0);
    lv_obj_set_style_bg_color(btn, COL_COPPER_D, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, COL_OFFWHITE, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
    return btn;
}

void HomeMenuView::Create(lv_obj_t* root) {
    lv_obj_set_style_bg_color(root, COL_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    status_bar.Attach(root);

    const lv_coord_t kFirstY = StatusBar::HEIGHT_PX + 8;
    const lv_coord_t kStepY  = 38;

    ui.btn_recorder = makeMenuButton(root, "Recorder",  kFirstY + 0 * kStepY);
    ui.btn_files    = makeMenuButton(root, "Files",     kFirstY + 1 * kStepY);
    ui.btn_settings = makeMenuButton(root, "Settings",  kFirstY + 2 * kStepY);
    ui.btn_sleep    = makeMenuButton(root, "Sleep",     kFirstY + 3 * kStepY);
    ui.btn_poweroff = makeMenuButton(root, "Power Off", kFirstY + 4 * kStepY);
}

void HomeMenuView::Delete() {
    status_bar.Detach();
}

void HomeMenuView::SetSleepEnabled(bool enabled) {
    if (!ui.btn_sleep) return;
    if (enabled) {
        lv_obj_clear_state(ui.btn_sleep, LV_STATE_DISABLED);
        lv_obj_clear_flag(ui.btn_sleep, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Hide when recording (FR25): cleaner than greying out on a 5-row menu.
        lv_obj_add_flag(ui.btn_sleep, LV_OBJ_FLAG_HIDDEN);
    }
}
