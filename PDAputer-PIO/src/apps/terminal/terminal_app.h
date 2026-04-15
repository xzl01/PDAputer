#pragma once

#include "../../app_manager/app_base.h"
#include "../../app_manager/app_manager.h"
#include <WiFi.h>
#include <lvgl.h>
#include <smooth_ui_toolkit.h>
#include <libssh/libssh.h>
#include <libssh_esp32.h>
#include <stdint.h>

using namespace smooth_ui_toolkit;

enum class TerminalMode {
    ECHO,      // Local echo (testing)
    UART,      // UART passthrough
    TELNET,    // Telnet connection
    SSH        // SSH connection (disabled for now)
};

class TerminalApp : public AppBase {
public:
    TerminalApp(AppManager& manager) : _manager(manager) {}

    void setBackApp(AppBase* back) { _back_app = back; }

    void onCreate() override;
    void onUpdate() override;
    void onDestroy() override;
    void onKeyPressed(char key) override;
    const char* getName() override { return "Terminal"; }

private:
    static constexpr int INPUT_MAX = 120;
    static constexpr int LINE_BUF_SIZE = 256;
    static constexpr int TERM_COLS = 30;
    static constexpr int TERM_ROWS = 8;
    static constexpr int TERM_HISTORY = 128;
    static constexpr int ANSI_BUF_SIZE = 64;

    AppManager& _manager;
    AppBase* _back_app = nullptr;

    lv_obj_t* _screen = nullptr;
    lv_obj_t* _status_bar = nullptr;
    lv_obj_t* _term_container = nullptr;
    lv_obj_t* _spangroups[TERM_ROWS] = {nullptr};
    lv_obj_t* _term_ta = nullptr;
    lv_obj_t* _input_container = nullptr;
    lv_obj_t* _input_label = nullptr;
    lv_obj_t* _mode_label = nullptr;
    lv_obj_t* _prompt_label = nullptr;

    char _input_buf[INPUT_MAX + 1] = {0};
    int _input_len = 0;
    bool _cursor_visible = true;
    uint32_t _cursor_timer = 0;

    TerminalMode _mode = TerminalMode::ECHO;

    bool _serial_inited = false;

    bool _net_connected = false;
    WiFiClient _telnet_client;
    char _telnet_host[64] = {0};
    uint16_t _telnet_port = 23;

    ssh_session _ssh_session = nullptr;
    ssh_channel _ssh_channel = nullptr;
    bool _ssh_connected = false;
    bool _ssh_waiting_password = false;
    char _ssh_host[64] = {0};
    char _ssh_user[32] = {0};
    uint16_t _ssh_port = 22;

    char _line_buf[LINE_BUF_SIZE];
    uint16_t _line_fg[LINE_BUF_SIZE] = {0};
    uint16_t _line_bg[LINE_BUF_SIZE] = {0};
    int _line_len = 0;
    int _cursor_col = 0;
    char _history[TERM_HISTORY][TERM_COLS + 1] = {{0}};
    uint16_t _fg_history[TERM_HISTORY][TERM_COLS] = {{0}};
    uint16_t _bg_history[TERM_HISTORY][TERM_COLS] = {{0}};
    int _history_count = 0;
    int _scroll_offset = 0;
    uint16_t _current_fg = 0xFFFF;
    uint16_t _current_bg = 0x0000;
    bool _in_esc = false;
    bool _in_csi = false;
    char _ansi_buf[ANSI_BUF_SIZE] = {0};
    int _ansi_len = 0;

    // ========================================
    // Working buffers (formerly static locals)
    // ========================================
    char _echo_buf[INPUT_MAX + 16] = {0};
    char _unknown_cmd_buf[160] = {0};
    char _cursor_buf[INPUT_MAX + 8] = {0};
    char _telnet_cmd_buf[INPUT_MAX + 4] = {0};
    char _info_buf[96] = {0};
    char _ssh_err_buf[160] = {0};
    char _color_buf[TERM_COLS + 1] = {0};
    char _color_buf2[TERM_COLS + 1] = {0};
    char _gray_buf[TERM_COLS + 1] = {0};


    void backendInit();
    void backendLoop();
    void backendSend(const char* str, int len);
    void backendClose();
    bool backendConnected();
    void telnetConnect(const char* host, uint16_t port);
    void telnetDisconnect();
    void sshConnect(const char* host, const char* user, uint16_t port, const char* password);
    void sshDisconnect();

    void printLine(const char* line);
    void putChar(char c);
    void handleEnter();
    void handleBackspace();
    void handleChar(char c);
    void clearInput();
    void updateCursor();
    void updateInputDisplay();
    void updateStatus();
    void switchMode();
    void setMode(TerminalMode m);
    void appendWrappedLine(const char* line);
    void renderTerminal();
    void pushHistoryRow(const char* text, const uint16_t* fg, const uint16_t* bg, size_t len);
    void renderSpanLine(int row, int history_index);
    uint16_t ansiColorTo565(int idx);
    void resetAnsiState();
    void handleAnsiSequence(const char* seq);

    Animate _entrance;
    bool _entering = false;
};
