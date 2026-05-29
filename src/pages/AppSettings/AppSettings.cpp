#include "AppSettings.h"

using namespace Page;

// App-level brightness. Initialised to firmware default; updated by the
// slider. main.cpp queries this at boot to set the initial brightness.
uint8_t Page::g_app_brightness = DEFAULT_BRIGHTNESS;
uint8_t Page::GetCurrentBrightness() { return g_app_brightness; }

#define COL_BG       lv_color_hex(0x1A1A1A)
#define COL_COPPER   lv_color_hex(0xC78C5C)
#define COL_OFFWHITE lv_color_hex(0xEDE8E2)
#define COL_GREY     lv_color_hex(0xA8A39D)
#define COL_SURFACE  lv_color_hex(0x2A2A2A)

AppSettings::AppSettings() {}
AppSettings::~AppSettings() {}

void AppSettings::onCustomAttrConfig() { LV_LOG_USER(__func__); }

void AppSettings::onViewLoad() {
    LV_LOG_USER(__func__);
    ui.root = _root;
    lv_obj_set_style_bg_color(_root, COL_BG, 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    status_bar.Attach(_root);
    Build();
}

void AppSettings::onViewDidLoad()        { LV_LOG_USER(__func__); }
void AppSettings::onViewWillAppear()     { LV_LOG_USER(__func__); }
void AppSettings::onViewDidAppear()      { LV_LOG_USER(__func__); }
void AppSettings::onViewWillDisappear()  { LV_LOG_USER(__func__); }
void AppSettings::onViewDidDisappear()   { LV_LOG_USER(__func__); }
void AppSettings::onViewUnload()         { LV_LOG_USER(__func__); status_bar.Detach(); }
void AppSettings::onViewDidUnload()      { LV_LOG_USER(__func__); }

void AppSettings::Build() {
    const lv_coord_t kY0 = StatusBar::HEIGHT_PX + 30;

    ui.label_brightness = lv_label_create(_root);
    lv_obj_set_style_text_color(ui.label_brightness, COL_OFFWHITE, 0);
    lv_obj_set_style_text_font(ui.label_brightness, &lv_font_montserrat_16, 0);
    lv_obj_align(ui.label_brightness, LV_ALIGN_TOP_MID, 0, kY0);
    lv_label_set_text_fmt(ui.label_brightness, "Brightness: %u", g_app_brightness);

    ui.slider_brightness = lv_slider_create(_root);
    lv_obj_set_size(ui.slider_brightness, 220, 14);
    lv_obj_align(ui.slider_brightness, LV_ALIGN_TOP_MID, 0, kY0 + 40);
    lv_slider_set_range(ui.slider_brightness, 10, 255);
    lv_slider_set_value(ui.slider_brightness, g_app_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui.slider_brightness, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.slider_brightness, COL_COPPER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui.slider_brightness, COL_COPPER, LV_PART_KNOB);
    lv_obj_add_event_cb(ui.slider_brightness, onSlider, LV_EVENT_VALUE_CHANGED, this);

    ui.btn_menu = lv_btn_create(_root);
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
    lv_obj_add_event_cb(ui.btn_menu, onMenu, LV_EVENT_CLICKED, this);
}

void AppSettings::onSlider(lv_event_t* e) {
    AppSettings* self = (AppSettings*)lv_event_get_user_data(e);
    int v = lv_slider_get_value(self->ui.slider_brightness);
    if (v < 0) v = 0; if (v > 255) v = 255;
    g_app_brightness = (uint8_t)v;
    M5.Display.setBrightness(g_app_brightness);
    lv_label_set_text_fmt(self->ui.label_brightness, "Brightness: %u", g_app_brightness);
}

void AppSettings::onMenu(lv_event_t* e) {
    AppSettings* self = (AppSettings*)lv_event_get_user_data(e);
    self->_Manager->Replace("Pages/HomeMenu");
}
