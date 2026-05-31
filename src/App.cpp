#include "PageManager.h"
#include "res/ResourcePool.h"
#include "pages/AppFactory.h"
#include "pages/AppRecorder/AppRecorderModel.h"
#include "pages/AppSettings/AppSettings.h"
#include "M5Unified.h"
#include "config.h"

// Screen auto-off state (FR19-FR21). LVGL tracks input inactivity via
// lv_disp_get_inactive_time(); we just decide when to dim and when to wake.
static bool g_backlight_on = true;

// Polled every 200 ms. Auto-off (FR19-21) exists to save battery/burn-in during
// a long recording — when the device sits on a table capturing a meeting. When
// the device is IDLE (not recording) the screen stays on: a blanked idle screen
// is indistinguishable from a dead device and made field/bench testing read every
// healthy boot as "dead". So: only blank while a recording is active; otherwise
// keep the backlight on. Touch still resets the idle timer during recording.
static void screen_idle_tick(lv_timer_t* t) {
    (void)t;
    bool recording = Page::g_app_recorder_model &&
                     Page::g_app_recorder_model->IsRecording();
    uint32_t inactive = lv_disp_get_inactive_time(NULL);
    if (recording && inactive >= SCREEN_IDLE_TIMEOUT_MS) {
        if (g_backlight_on) {
            M5.Display.setBrightness(0);
            g_backlight_on = false;
        }
    } else {
        if (!g_backlight_on) {
            M5.Display.setBrightness(Page::GetCurrentBrightness());
            g_backlight_on = true;
        }
    }
}

void App_Init() {
    static AppFactory factory;
    static PageManager manager(&factory);

    // FR35: instantiate the app-level recorder model singleton.
    static Page::AppRecorderModel s_recorder_model;
    Page::g_app_recorder_model = &s_recorder_model;

    /* Make sure the default group exists */
    if (!lv_group_get_default()) {
        lv_group_t* group = lv_group_create();
        lv_group_set_default(group);
    }

#if MONKEY_TEST_ENABLE
    lv_monkey_config_t config;
    lv_monkey_config_init(&config);
    config.type = LV_INDEV_TYPE_POINTER;
    config.period_range.min = 20;
    config.period_range.max = 50;
    config.input_range.min = 0;
    config.input_range.max = 320;
    lv_monkey_t* monkey = lv_monkey_create(&config);
    lv_monkey_set_enable(monkey, true);

    lv_group_t* group = lv_group_create();
    lv_indev_set_group(lv_monkey_get_indev(monkey), group);
    lv_group_set_default(group);

    LV_LOG_USER("lv_monkey test started!");
#endif
    /* Set screen style */
    lv_obj_t* scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_disp_set_bg_color(lv_disp_get_default(), lv_color_white());

    /* Set root default style */
    static lv_style_t rootStyle;
    lv_style_init(&rootStyle);
    lv_style_set_width(&rootStyle, LV_HOR_RES);
    lv_style_set_height(&rootStyle, LV_VER_RES);
    lv_style_set_border_color(&rootStyle, lv_color_make(238, 238, 238));
    lv_style_set_border_width(&rootStyle, 0);
    lv_style_set_outline_width(&rootStyle, 0);
    lv_style_set_pad_all(&rootStyle, 0);
    manager.SetRootDefaultStyle(&rootStyle);

    /* Initialize resource pool */
    ResourcePool::Init();

    /* Initialize pages */
    manager.Install("StartUp", "Pages/StartUp");
    manager.Install("HomeMenu", "Pages/HomeMenu");
    manager.Install("AppWiFi", "Pages/AppWiFi");
    manager.Install("AppCamera", "Pages/AppCamera");
    manager.Install("AppMic", "Pages/AppMic");
    manager.Install("AppPower", "Pages/AppPower");
    manager.Install("AppIMU", "Pages/AppIMU");
    manager.Install("AppSD", "Pages/AppSD");
    manager.Install("AppTouch", "Pages/AppTouch");
    manager.Install("AppI2C", "Pages/AppI2C");
    manager.Install("AppRTC", "Pages/AppRTC");
    manager.Install("AppRecorder", "Pages/AppRecorder");
    manager.Install("AppFiles", "Pages/AppFiles");
    manager.Install("AppSettings", "Pages/AppSettings");

    manager.SetGlobalLoadAnimType(PageManager::LOAD_ANIM_NONE);

    // Boot landing page = recorder screen (FR24 success criterion).
    manager.Push("Pages/AppRecorder");

    // Screen auto-off ticker (FR19-FR21).
    lv_timer_create(screen_idle_tick, 200, nullptr);
}

void App_Uninit() {
    Page::g_app_recorder_model = nullptr;
}
