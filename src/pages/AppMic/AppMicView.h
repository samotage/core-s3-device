#ifndef __APPMIC_VIEW_H
#define __APPMIC_VIEW_H

#include "../Page.h"

namespace Page {

class AppMicView {
   public:
    void Create(lv_obj_t* root);
    void Delete();
    void SetRecording(bool is_recording, uint32_t seconds = 0, const char* filename = nullptr);

   public:
    struct {
        lv_obj_t* img_bg;
        lv_obj_t* imgbtn_home;
        lv_obj_t* imgbtn_next;
        lv_obj_t* btn_top_center;
        lv_obj_t* btn_record;
        lv_obj_t* label_record;
        lv_obj_t* label_status;
    } ui;

   private:
};

}  // namespace Page

#endif
