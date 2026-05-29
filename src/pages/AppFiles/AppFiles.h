#ifndef __APPFILES_H
#define __APPFILES_H

#include "AppFilesView.h"
#include "AppFilesModel.h"

namespace Page {

class AppFiles : public PageBase {
   public:
    AppFiles();
    virtual ~AppFiles();

    virtual void onCustomAttrConfig();
    virtual void onViewLoad();
    virtual void onViewDidLoad();
    virtual void onViewWillAppear();
    virtual void onViewDidAppear();
    virtual void onViewWillDisappear();
    virtual void onViewDidDisappear();
    virtual void onViewUnload();
    virtual void onViewDidUnload();

    void RefreshStats();

    AppFilesView View;
    AppFilesModel Model;

   private:
    static void onEvent(lv_event_t* event);
};

}  // namespace Page

#endif
