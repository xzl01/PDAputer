#pragma once

#include "app_base.h"

class AppManager {
public:
    void init();
    void update();
    void startApp(AppBase* app);
    void switchApp(AppBase* app);
    void handleKey(char key);
    AppBase* currentApp();

private:
    AppBase* _current_app = nullptr;
    AppBase* _next_app = nullptr;
};
