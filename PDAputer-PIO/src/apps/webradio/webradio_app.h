#pragma once

#include "../../app_manager/app_base.h"
#include "../../app_manager/app_manager.h"
#include <lvgl.h>
#include <driver/i2s.h>
typedef i2s_port_t* i2s_chan_handle_t;

struct WebRadioStation {
    const char* title;
    const char* url;
};

class WebRadioApp : public AppBase {
public:
    WebRadioApp(AppManager& manager) : _manager(manager) {}

    void setBackApp(AppBase* back) { _back_app = back; }

    void onCreate() override;
    void onUpdate() override;
    void onDestroy() override;
    void onKeyPressed(char key) override;
    const char* getName() override { return "WebRadio"; }

private:
    AppManager& _manager;
    AppBase* _back_app = nullptr;

    static constexpr int MAX_STATIONS = 16;
    static constexpr int RING_BUF_SIZE = 32768;

    lv_obj_t* _screen = nullptr;
    lv_obj_t* _label_station = nullptr;
    lv_obj_t* _label_status = nullptr;
    lv_obj_t* _label_station_nos = nullptr;
    lv_obj_t* _arc_volume = nullptr;
    lv_obj_t* _panel_controls = nullptr;
    lv_obj_t* _img_prev = nullptr;
    lv_obj_t* _img_play = nullptr;
    lv_obj_t* _img_next = nullptr;
    lv_obj_t* _img_signal = nullptr;
    lv_timer_t* _tmr = nullptr;

    int _station_count = 0;
    int _selected = 0;
    int16_t _volume = 16;

    bool _playing = false;
    bool _connecting = false;

    i2s_chan_handle_t _i2s_tx = nullptr;
    bool _i2s_running = false;

    uint8_t* _ring_buf = nullptr;
    volatile int _ring_head = 0;
    volatile int _ring_tail = 0;
    volatile int _ring_count = 0;

    TaskHandle_t _net_task = nullptr;
    volatile bool _net_task_run = false;
    TaskHandle_t _audio_task = nullptr;
    volatile bool _audio_task_run = false;

    volatile bool _stop_requested = false;
    volatile bool _stream_error = false;

    void setupI2S(uint32_t sample_rate, bool stereo);
    void teardownI2S();

    void playStation(int idx);
    void stopPlayback();
    void goBack();
    void updateStationUI();
    void updatePlayPauseUI();
    void updateVolumeUI();
    void updateStatusUI();
    void flashControl(lv_obj_t* obj);

    int ringWrite(const uint8_t* data, int len);
    int ringRead(uint8_t* data, int len);

    static void netTaskFunc(void* param);
    static void audioTaskFunc(void* param);
    static void timerCb(lv_timer_t* t);
};
