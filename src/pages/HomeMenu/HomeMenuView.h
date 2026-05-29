#ifndef __HOMEMENU_VIEW_H
#define __HOMEMENU_VIEW_H

#include "../Page.h"
#include "../_widgets/StatusBar.h"

namespace Page {

class HomeMenuView {
   public:
    void Create(lv_obj_t* root);
    void Delete();
    void SetSleepEnabled(bool enabled);

   public:
    struct {
        // Five purpose-built entries (FR10).
        lv_obj_t* btn_recorder;
        lv_obj_t* btn_files;
        lv_obj_t* btn_settings;
        lv_obj_t* btn_sleep;
        lv_obj_t* btn_poweroff;
    } ui;

    StatusBar status_bar;
};

}  // namespace Page

#endif  // !__VIEW_H
