#ifndef __APPRECORDER_VIEW_H
#define __APPRECORDER_VIEW_H

#include "../Page.h"
#include "../_widgets/StatusBar.h"

namespace Page {

class AppRecorderView {
   public:
    void Create(lv_obj_t* root);
    void Delete();

    void SetIdle();
    void SetRecording(uint32_t seconds);
    void SetSaved(const char* filename, uint32_t seconds);
    void SetError(const char* msg);
    void SetStorageFull();
    void SetLevel(uint8_t level);  // 0..100

   public:
    struct {
        lv_obj_t* root;
        lv_obj_t* img_logo;
        lv_obj_t* btn_record;
        lv_obj_t* label_btn;
        lv_obj_t* label_timer;
        lv_obj_t* bar_level;
        lv_obj_t* label_status;
        lv_obj_t* btn_menu;
        lv_obj_t* btn_to_files;  // shown when storage full
    } ui;

    StatusBar status_bar;
};

}  // namespace Page

#endif
