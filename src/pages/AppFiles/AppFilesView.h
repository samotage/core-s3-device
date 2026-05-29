#ifndef __APPFILES_VIEW_H
#define __APPFILES_VIEW_H

#include "../Page.h"
#include "../_widgets/StatusBar.h"
#include "AppFilesModel.h"

namespace Page {

class AppFilesView {
   public:
    void Create(lv_obj_t* root);
    void Delete();
    void SetStats(const FilesStats& s);
    void SetBusy(bool busy);

   public:
    struct {
        lv_obj_t* root;
        lv_obj_t* label_files;
        lv_obj_t* label_storage;
        lv_obj_t* label_est;
        lv_obj_t* btn_delete10;
        lv_obj_t* btn_menu;
        lv_obj_t* spinner;
    } ui;

    StatusBar status_bar;
};

}  // namespace Page

#endif
