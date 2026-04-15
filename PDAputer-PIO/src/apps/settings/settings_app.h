#pragma once

#include "../../app_manager/app_base.h"
#include "../../app_manager/app_manager.h"
#include <lvgl.h>
#include <smooth_ui_toolkit.h>

using namespace smooth_ui_toolkit;

struct WifiAP {
    char ssid[33];
    int8_t rssi;
    uint8_t enc;
};

class SettingsApp : public AppBase {
public:
    SettingsApp(AppManager& manager) : _manager(manager) {}
    void setBackApp(AppBase* back) { _back_app = back; }
    void onCreate() override;
    void onUpdate() override;
    void onDestroy() override;
    void onKeyPressed(char key) override;
    const char* getName() override { return "Settings"; }

private:
    AppManager& _manager;
    AppBase* _back_app = nullptr;
    static constexpr int MENU_COUNT = 7;
    int _sel = 0;
    bool _editing = false;
    bool _on_support_screen = false;
    int _timezone_index = 0;
    lv_obj_t* _containers[MENU_COUNT] = {};

    int _wifi_state = 0; // 0=idle, 1=polling, 2=list, 3=password, 4=connecting, 5=done, 10=prepare mode, 11=start scan
    static constexpr int MAX_AP = 16;
    WifiAP _aps[MAX_AP];
    int _ap_count = 0;
    int _ap_sel = 0;
    char _pwd[65] = {0};
    int _pwd_len = 0;
    lv_obj_t* _wifi_screen = nullptr;
    lv_obj_t* _wifi_status = nullptr;
    lv_obj_t* _wifi_list = nullptr;
    lv_obj_t* _wifi_pwd_label = nullptr;
    unsigned long _wifi_connect_start = 0;

    void wifiEnterScan();
    void wifiPollScan();
    void wifiBuildList();
    void wifiEnterPassword();
    void wifiConnect();
    void wifiClose();
    void wifiUpdatePwdDisplay();
    void updateWifiSelectionStyles();

    void loadValues();
    void updateSelection();
    void enterEdit();
    void exitEdit(bool save);
    void handleEditKey(char key);
    void saveBrightness();
    void syncTimezoneLabel();

    Animate _entrance;
    bool _entering = false;
    AnimateValue _sel_bounce;
    int _prev_sel = 0;
};
