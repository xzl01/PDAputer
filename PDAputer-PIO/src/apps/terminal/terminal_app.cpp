#include "terminal_app.h"

#include "lvgl.h"
#include <ui.h>
#include <lvgl.h>

// Font declarations (from lib/ui/fonts.h)
extern const lv_font_t ui_font_noto_30;
extern const lv_font_t ui_font_noto_20;
extern const lv_font_t ui_font_noto_25;
extern const lv_font_t ui_font_noto_16;
extern const lv_font_t ui_font_noto_12;

#include <Arduino.h>
#include <M5Cardputer.h>
#include <config_manager.h>
#include <wifi_manager.h>
#include <keyboard_manager.h>
#include <cstring>
#include <cstdlib>

// ============================================================
// ANSI / character accumulator
// ============================================================

uint16_t TerminalApp::ansiColorTo565(int idx) {
    static const uint16_t base16[16] = {
        0x0000, 0x8000, 0x0400, 0x8400, 0x0010, 0x8010, 0x0410, 0xC618,
        0x7BEF, 0xF800, 0x07E0, 0xFFE0, 0x001F, 0xF81F, 0x07FF, 0xFFFF
    };
    if (idx < 0) return 0xFFFF;
    if (idx < 16) return base16[idx];
    if (idx >= 16 && idx <= 231) {
        int n = idx - 16;
        int r = n / 36;
        int g = (n / 6) % 6;
        int b = n % 6;
        int rv = r == 0 ? 0 : 55 + r * 40;
        int gv = g == 0 ? 0 : 55 + g * 40;
        int bv = b == 0 ? 0 : 55 + b * 40;
        return ((rv & 0xF8) << 8) | ((gv & 0xFC) << 3) | (bv >> 3);
    }
    if (idx >= 232 && idx <= 255) {
        int v = 8 + (idx - 232) * 10;
        return ((v & 0xF8) << 8) | ((v & 0xFC) << 3) | (v >> 3);
    }
    return 0xFFFF;
}

void TerminalApp::resetAnsiState() {
    _current_fg = 0xFFFF;
    _current_bg = 0x0000;
}

void TerminalApp::handleAnsiSequence(const char* seq) {
    if (!seq || !*seq) return;
    if (seq[strlen(seq) - 1] != 'm') return;

    char tmp[ANSI_BUF_SIZE];
    strlcpy(tmp, seq, sizeof(tmp));
    tmp[strlen(tmp) - 1] = '\0';

    char* saveptr = nullptr;
    char* tok = strtok_r(tmp, ";", &saveptr);
    int vals[8];
    int count = 0;
    while (tok && count < 8) {
        vals[count++] = atoi(tok);
        tok = strtok_r(nullptr, ";", &saveptr);
    }
    if (count == 0) {
        resetAnsiState();
        return;
    }

    for (int i = 0; i < count; ++i) {
        int v = vals[i];
        if (v == 0) {
            resetAnsiState();
        } else if (v >= 30 && v <= 37) {
            _current_fg = ansiColorTo565(v - 30);
        } else if (v >= 90 && v <= 97) {
            _current_fg = ansiColorTo565(v - 90 + 8);
        } else if (v >= 40 && v <= 47) {
            _current_bg = ansiColorTo565(v - 40);
        } else if (v >= 100 && v <= 107) {
            _current_bg = ansiColorTo565(v - 100 + 8);
        } else if (v == 39) {
            _current_fg = 0xFFFF;
        } else if (v == 49) {
            _current_bg = 0x0000;
        } else if (v == 38 && i + 2 < count && vals[i + 1] == 5) {
            _current_fg = ansiColorTo565(vals[i + 2]);
            i += 2;
        } else if (v == 48 && i + 2 < count && vals[i + 1] == 5) {
            _current_bg = ansiColorTo565(vals[i + 2]);
            i += 2;
        }
    }
}

void TerminalApp::putChar(char c) {
    if (_in_esc) {
        if (!_in_csi) {
            if (c == '[') {
                _in_csi = true;
                _ansi_len = 0;
                _ansi_buf[0] = '\0';
            } else {
                _in_esc = false;
            }
            return;
        }

        if (_ansi_len < ANSI_BUF_SIZE - 1) {
            _ansi_buf[_ansi_len++] = c;
            _ansi_buf[_ansi_len] = '\0';
        }

        if ((c >= '@' && c <= '~')) {
            handleAnsiSequence(_ansi_buf);
            _in_esc = false;
            _in_csi = false;
            _ansi_len = 0;
            _ansi_buf[0] = '\0';
        }
        return;
    }

    if (c == 0x1B) {
        _in_esc = true;
        _in_csi = false;
        _ansi_len = 0;
        _ansi_buf[0] = '\0';
        return;
    }

    if (c == '\r') {
        _cursor_col = 0;
        return;
    }

    if (c == '\n') {
        if (_line_len > 0) {
            _line_buf[_line_len] = '\0';
            appendWrappedLine(_line_buf);
        } else {
            uint16_t fg[TERM_COLS];
            uint16_t bg[TERM_COLS];
            for (int i = 0; i < TERM_COLS; ++i) {
                fg[i] = _current_fg;
                bg[i] = _current_bg;
            }
            pushHistoryRow("", fg, bg, 0);
        }
        _scroll_offset = 0;
        renderTerminal();
        _line_len = 0;
        _cursor_col = 0;
        _line_buf[0] = '\0';
        return;
    }

    if (c == '\b') {
        if (_cursor_col > 0) {
            _cursor_col--;
            if (_cursor_col < _line_len) {
                _line_len = _cursor_col;
                _line_buf[_line_len] = '\0';
            }
        }
        return;
    }

    if (c >= 32 && _cursor_col < LINE_BUF_SIZE - 1) {
        _line_buf[_cursor_col] = c;
        _line_fg[_cursor_col] = _current_fg;
        _line_bg[_cursor_col] = _current_bg;
        _cursor_col++;
        if (_cursor_col > _line_len) {
            _line_len = _cursor_col;
            _line_buf[_line_len] = '\0';
        }
    }
}

// ============================================================
// Fixed-grid terminal rendering
// ============================================================

void TerminalApp::pushHistoryRow(const char* text, const uint16_t* fg, const uint16_t* bg, size_t len) {
    if (_history_count < TERM_HISTORY) {
        memset(_history[_history_count], 0, sizeof(_history[_history_count]));
        memset(_fg_history[_history_count], 0xFF, sizeof(_fg_history[_history_count]));
        memset(_bg_history[_history_count], 0x00, sizeof(_bg_history[_history_count]));
        if (len > TERM_COLS) len = TERM_COLS;
        memcpy(_history[_history_count], text, len);
        memcpy(_fg_history[_history_count], fg, len * sizeof(uint16_t));
        memcpy(_bg_history[_history_count], bg, len * sizeof(uint16_t));
        _history[_history_count][len] = '\0';
        _history_count++;
    } else {
        memmove(_history, _history + 1, sizeof(_history[0]) * (TERM_HISTORY - 1));
        memmove(_fg_history, _fg_history + 1, sizeof(_fg_history[0]) * (TERM_HISTORY - 1));
        memmove(_bg_history, _bg_history + 1, sizeof(_bg_history[0]) * (TERM_HISTORY - 1));
        memset(_history[TERM_HISTORY - 1], 0, sizeof(_history[TERM_HISTORY - 1]));
        memset(_fg_history[TERM_HISTORY - 1], 0xFF, sizeof(_fg_history[TERM_HISTORY - 1]));
        memset(_bg_history[TERM_HISTORY - 1], 0x00, sizeof(_bg_history[TERM_HISTORY - 1]));
        if (len > TERM_COLS) len = TERM_COLS;
        memcpy(_history[TERM_HISTORY - 1], text, len);
        memcpy(_fg_history[TERM_HISTORY - 1], fg, len * sizeof(uint16_t));
        memcpy(_bg_history[TERM_HISTORY - 1], bg, len * sizeof(uint16_t));
        _history[TERM_HISTORY - 1][len] = '\0';
    }
}

void TerminalApp::appendWrappedLine(const char* line) {
    if (!line) return;

    size_t len = strlen(line);
    if (len == 0) {
        uint16_t fg[TERM_COLS];
        uint16_t bg[TERM_COLS];
        for (int i = 0; i < TERM_COLS; ++i) { fg[i] = _current_fg; bg[i] = _current_bg; }
        pushHistoryRow("", fg, bg, 0);
        return;
    }

    const uint16_t* src_fg = (len > 0) ? _line_fg : nullptr;
    const uint16_t* src_bg = (len > 0) ? _line_bg : nullptr;

    for (size_t i = 0; i < len; i += TERM_COLS) {
        char row[TERM_COLS + 1];
        uint16_t fg[TERM_COLS];
        uint16_t bg[TERM_COLS];
        size_t chunk = len - i;
        if (chunk > TERM_COLS) chunk = TERM_COLS;
        memcpy(row, line + i, chunk);
        row[chunk] = '\0';
        for (size_t j = 0; j < chunk; ++j) {
            fg[j] = src_fg ? src_fg[i + j] : _current_fg;
            bg[j] = src_bg ? src_bg[i + j] : _current_bg;
        }
        pushHistoryRow(row, fg, bg, chunk);
    }
}

void TerminalApp::buildRecolorLine(int history_index, char* out, size_t out_size) {
    out[0] = '\0';
    if (history_index < 0 || history_index >= _history_count) return;

    char color_tag[16];
    bool has_open_color = false;
    uint16_t last_fg = 0xFFFF;
    for (int col = 0; col < TERM_COLS && _history[history_index][col]; ++col) {
        uint16_t fg = _fg_history[history_index][col];
        if (fg != last_fg) {
            if (has_open_color) strlcat(out, "#", out_size);
            int r = ((fg >> 11) & 0x1F) * 255 / 31;
            int g = ((fg >> 5) & 0x3F) * 255 / 63;
            int b = (fg & 0x1F) * 255 / 31;
            snprintf(color_tag, sizeof(color_tag), "#%02x%02x%02x ", r, g, b);
            strlcat(out, color_tag, out_size);
            last_fg = fg;
            has_open_color = true;
        }

        char c = _history[history_index][col];
        if (c == '#') {
            strlcat(out, "##", out_size);
        } else if (c == '\\') {
            strlcat(out, "\\\\", out_size);
        } else {
            char ch[2] = {c, '\0'};
            strlcat(out, ch, out_size);
        }
    }
    if (has_open_color) strlcat(out, "#", out_size);
}

void TerminalApp::renderTerminal() {
    if (!_term_container) return;

    int max_scroll = _history_count > TERM_ROWS ? (_history_count - TERM_ROWS) : 0;
    if (_scroll_offset < 0) _scroll_offset = 0;
    if (_scroll_offset > max_scroll) _scroll_offset = max_scroll;

    int start = _history_count - TERM_ROWS - _scroll_offset;
    if (start < 0) start = 0;

    for (int row = 0; row < TERM_ROWS; ++row) {
        int idx = start + row;
        if (!_term_lines[row]) continue;
        if (idx < _history_count) {
            buildRecolorLine(idx, _rendered_lines[row], sizeof(_rendered_lines[row]));
            lv_label_set_text(_term_lines[row], _rendered_lines[row]);
        } else {
            lv_label_set_text(_term_lines[row], "");
        }
    }
}

void TerminalApp::printLine(const char* line) {
    if (!line) line = "";
    for (const char* p = line; *p; ++p) {
        putChar(*p);
    }
    putChar('\n');
}

// ============================================================
// Lifecycle
// ============================================================

void TerminalApp::onCreate() {
    _input_len = 0;
    _input_buf[0] = '\0';
    _mode = TerminalMode::ECHO;
    _serial_inited = false;
    _net_connected = false;
    _telnet_host[0] = '\0';
    _ssh_host[0] = '\0';
    _ssh_user[0] = '\0';
    _ssh_port = 22;
    _ssh_session = nullptr;
    _ssh_channel = nullptr;
    _ssh_connected = false;
    _ssh_waiting_password = false;
    _line_len = 0;
    _cursor_col = 0;
    _history_count = 0;
    _scroll_offset = 0;
    memset(_history, 0, sizeof(_history));
    memset(_line_buf, 0, sizeof(_line_buf));
    memset(_line_fg, 0xFF, sizeof(_line_fg));
    memset(_line_bg, 0x00, sizeof(_line_bg));
    _entering = false;
    _cursor_visible = true;
    _cursor_timer = millis();

    libssh_begin();

    lv_disp_t* disp = lv_disp_get_default();
    Serial.printf("[TERM] Display: hor=%d ver=%d\n",
        lv_disp_get_hor_res(disp), lv_disp_get_ver_res(disp));

    _screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(_screen);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_screen, 0, 0);
    lv_obj_set_style_border_width(_screen, 0, 0);
    lv_obj_set_scrollbar_mode(_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(_screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    _status_bar = lv_obj_create(_screen);
    lv_obj_remove_style_all(_status_bar);
    lv_obj_set_width(_status_bar, LV_PCT(100));
    lv_obj_set_height(_status_bar, 14);
    lv_obj_set_style_bg_color(_status_bar, lv_color_hex(0x181818), 0);
    lv_obj_set_style_bg_opa(_status_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(_status_bar, 3, 0);
    lv_obj_set_style_pad_right(_status_bar, 3, 0);
    lv_obj_set_style_pad_top(_status_bar, 0, 0);
    lv_obj_set_style_pad_bottom(_status_bar, 0, 0);
    lv_obj_set_layout(_status_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_status_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(_status_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(_status_bar, 0);

    _mode_label = lv_label_create(_status_bar);
    lv_label_set_text(_mode_label, "[ECHO]");
    lv_obj_set_style_text_color(_mode_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(_mode_label, &ui_font_noto_12, 0);

    _term_container = lv_obj_create(_screen);
    lv_obj_remove_style_all(_term_container);
    lv_obj_set_width(_term_container, LV_PCT(100));
    lv_obj_set_flex_grow(_term_container, 1);
    lv_obj_set_style_bg_color(_term_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_term_container, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(_term_container, 0, 0);
    lv_obj_set_style_pad_right(_term_container, 0, 0);
    lv_obj_set_style_pad_top(_term_container, 0, 0);
    lv_obj_set_style_pad_bottom(_term_container, 2, 0);
    lv_obj_set_scrollbar_mode(_term_container, LV_SCROLLBAR_MODE_OFF);

    _term_ta = nullptr;
    for (int i = 0; i < TERM_ROWS; ++i) {
        _term_lines[i] = lv_label_create(_term_container);
        lv_obj_set_width(_term_lines[i], LV_PCT(100));
        lv_obj_set_pos(_term_lines[i], 0, i * 12);
        lv_label_set_text(_term_lines[i], "");
        lv_label_set_long_mode(_term_lines[i], LV_LABEL_LONG_CLIP);
        lv_label_set_recolor(_term_lines[i], true);
        lv_obj_set_style_bg_color(_term_lines[i], lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(_term_lines[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(_term_lines[i], lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(_term_lines[i], &ui_font_noto_12, 0);
        lv_obj_set_style_border_width(_term_lines[i], 0, 0);
        lv_obj_set_style_pad_left(_term_lines[i], 0, 0);
        lv_obj_set_style_pad_right(_term_lines[i], 0, 0);
        lv_obj_set_style_pad_top(_term_lines[i], 0, 0);
        lv_obj_set_style_pad_bottom(_term_lines[i], 0, 0);
    }

    _input_container = lv_obj_create(_screen);
    lv_obj_remove_style_all(_input_container);
    lv_obj_set_width(_input_container, LV_PCT(100));
    lv_obj_set_height(_input_container, 14);
    lv_obj_set_style_bg_color(_input_container, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(_input_container, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(_input_container, 2, 0);
    lv_obj_set_style_pad_right(_input_container, 2, 0);
    lv_obj_set_style_pad_top(_input_container, 0, 0);
    lv_obj_set_style_pad_bottom(_input_container, 0, 0);
    lv_obj_set_layout(_input_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_input_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_input_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _prompt_label = lv_label_create(_input_container);
    lv_label_set_text(_prompt_label, "$ ");
    lv_obj_set_style_text_color(_prompt_label, lv_color_hex(0x55ff55), 0);
    lv_obj_set_style_text_font(_prompt_label, &ui_font_noto_12, 0);

    _input_label = lv_textarea_create(_input_container);
    lv_obj_set_height(_input_label, LV_PCT(100));
    lv_obj_set_flex_grow(_input_label, 1);
    lv_textarea_set_one_line(_input_label, true);
    lv_textarea_set_max_length(_input_label, INPUT_MAX);
    lv_textarea_set_text(_input_label, "");
    lv_obj_set_scrollbar_mode(_input_label, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(_input_label, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(_input_label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(_input_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(_input_label, &ui_font_noto_12, 0);
    lv_obj_set_style_border_width(_input_label, 0, 0);
    lv_obj_set_style_pad_left(_input_label, 0, 0);
    lv_obj_set_style_pad_right(_input_label, 0, 0);
    lv_obj_set_style_pad_top(_input_label, 0, 0);
    lv_obj_set_style_pad_bottom(_input_label, 0, 0);

    lv_scr_load(_screen);
    lv_obj_update_layout(_screen);
    Serial.printf("[TERM] layout: screen=%dx%d container=%dx%d input=%dx%d\n",
        lv_obj_get_width(_screen), lv_obj_get_height(_screen),
        lv_obj_get_width(_term_container), lv_obj_get_height(_term_container),
        lv_obj_get_width(_input_container), lv_obj_get_height(_input_container));

    printLine("Terminal v0.3");
    printLine("Modes: ECHO | UART | TELNET");
    printLine("TAB=mode | empty ENTER=back");
    printLine("Fn+; up | Fn+. down");
    printLine("");

    updateStatus();
    updateInputDisplay();

    _entrance.start = 0.0f;
    _entrance.end = 1.0f;
    _entrance.delay = 0.0f;
    _entrance.easingOptions().duration = 0.3f;
    _entrance.play();
    _entering = true;
}

void TerminalApp::onUpdate() {
    if (_entering) {
        _entrance.update();
        if (_entrance.done()) _entering = false;
    }

    backendLoop();

    // Cursor blink
    uint32_t now = millis();
    if (now - _cursor_timer > 500) {
        _cursor_timer = now;
        _cursor_visible = !_cursor_visible;
        updateCursor();
    }

    // Serial RX
    if (_mode == TerminalMode::UART && _serial_inited) {
        while (Serial1.available()) {
            putChar(Serial1.read());
        }
    }

    // Telnet RX
    if (_mode == TerminalMode::TELNET && _net_connected) {
        while (_telnet_client.available()) {
            putChar(_telnet_client.read());
        }
        if (!_telnet_client.connected()) {
            printLine("[TELNET] Disconnected");
            telnetDisconnect();
        }
    }

    // SSH RX
    if (_mode == TerminalMode::SSH && _ssh_connected && _ssh_channel) {
        char buffer[128];
        int n = 0;
        do {
            n = ssh_channel_read_nonblocking(_ssh_channel, buffer, sizeof(buffer), 0);
            for (int i = 0; i < n; ++i) putChar(buffer[i]);
        } while (n > 0);

        do {
            n = ssh_channel_read_nonblocking(_ssh_channel, buffer, sizeof(buffer), 1);
            for (int i = 0; i < n; ++i) putChar(buffer[i]);
        } while (n > 0);

        if (ssh_channel_is_eof(_ssh_channel) || ssh_channel_is_closed(_ssh_channel)) {
            printLine("[SSH] Disconnected");
            sshDisconnect();
        }
    }
}

void TerminalApp::onDestroy() {
    backendClose();
    // NOTE: Do NOT delete _screen. All other apps (Settings, Music, FMRadio, etc.)
    // do NOT delete their screen objects in onDestroy. LVGL manages screen lifecycle.
    // Deleting here causes lv_scr_load() to cache stale screen on second entry.
}

// ============================================================
// Keyboard input
// ============================================================

void TerminalApp::onKeyPressed(char key) {
    auto& kstate = M5Cardputer.Keyboard.keysState();

    if (kstate.fn) {
        if (key == ';') {
            _scroll_offset += 3;
            renderTerminal();
            return;
        }
        if (key == '.') {
            _scroll_offset -= 3;
            if (_scroll_offset < 0) _scroll_offset = 0;
            renderTerminal();
            return;
        }
    }

    // Long press ENTER on empty input = back to main menu
    if (key == KEY_CODE_ENTER && _input_len == 0) {
        if (_back_app) _manager.switchApp(_back_app);
        return;
    }

    if (key == KEY_CODE_ENTER) {
        handleEnter();
    } else if (key == KEY_CODE_BACKSPACE) {
        handleBackspace();
    } else if (key == KEY_CODE_TAB) {
        switchMode();
    } else if (key >= 32 && key < 127) {
        handleChar(key);
    }
}

// ============================================================
// Terminal operations
// ============================================================

void TerminalApp::handleEnter() {
    if (_input_len == 0) {
        printLine("$ ");
        clearInput();
        return;
    }

    // Echo command
    static char echo_buf[INPUT_MAX + 16];
    snprintf(echo_buf, sizeof(echo_buf), "$ %s", _input_buf);
    printLine(echo_buf);

    // Command parser for TELNET mode
    if (_mode == TerminalMode::TELNET) {
        if (strncmp(_input_buf, "connect ", 8) == 0) {
            const char* arg = _input_buf + 8;
            while (*arg == ' ') arg++;
            if (!*arg) { printLine("Usage: connect <host> [port]"); clearInput(); return; }

            uint16_t port = 23;
            char host[64] = {0};
            const char* colon = strchr(arg, ':');
            if (colon) {
                size_t hlen = colon - arg;
                if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
                strncpy(host, arg, hlen);
                port = atoi(colon + 1);
                if (port == 0) port = 23;
            } else {
                strncpy(host, arg, sizeof(host) - 1);
            }
            if (host[0]) telnetConnect(host, port);
            else printLine("Usage: connect <host> [port]");
            clearInput();
            return;
        }

        if (strcmp(_input_buf, "disconnect") == 0 ||
            strcmp(_input_buf, "exit") == 0) {
            telnetDisconnect();
            printLine("Disconnected.");
            clearInput();
            return;
        }
    }

    if (_mode == TerminalMode::SSH) {
        if (_ssh_waiting_password) {
            sshConnect(_ssh_host, _ssh_user, _ssh_port, _input_buf);
            clearInput();
            return;
        }

        if (strncmp(_input_buf, "connect ", 8) == 0) {
            char host[64] = {0};
            char user[32] = {0};
            uint16_t port = 22;

            int matched = sscanf(_input_buf + 8, "%63s %31s %hu", host, user, &port);
            if (matched >= 2) {
                strncpy(_ssh_host, host, sizeof(_ssh_host) - 1);
                strncpy(_ssh_user, user, sizeof(_ssh_user) - 1);
                _ssh_port = (matched >= 3 && port > 0) ? port : 22;
                _ssh_waiting_password = true;
                printLine("Password:");
            } else {
                printLine("Usage: connect <host> <user> [port]");
            }
            clearInput();
            return;
        }

        if (strcmp(_input_buf, "disconnect") == 0 || strcmp(_input_buf, "exit") == 0) {
            sshDisconnect();
            printLine("Disconnected.");
            clearInput();
            return;
        }
    }

    // ECHO mode commands
    if (_mode == TerminalMode::ECHO) {
        if (strcmp(_input_buf, "help") == 0) {
            printLine("\x1b[38;5;45mCommands:\x1b[0m \x1b[38;5;220mhelp\x1b[0m, \x1b[38;5;220mclear\x1b[0m, \x1b[38;5;220mabout\x1b[0m, \x1b[38;5;220mff\x1b[0m, \x1b[38;5;220mdemo\x1b[0m");
        } else if (strcmp(_input_buf, "clear") == 0) {
            _history_count = 0;
            _scroll_offset = 0;
            renderTerminal();
            clearInput();
            return;
        } else if (strcmp(_input_buf, "about") == 0) {
            printLine("\x1b[38;5;81mTerminal\x1b[0m \x1b[38;5;220mv0.3\x1b[0m for \x1b[38;5;118mPDAputer\x1b[0m");
        } else if (strcmp(_input_buf, "ff") == 0 || strcmp(_input_buf, "fastfetch") == 0 || strcmp(_input_buf, "neofetch") == 0) {
            printLine("\x1b[38;5;39mPDAputer\x1b[0m \x1b[38;5;219mfastfetch\x1b[0m");
            printLine("\x1b[38;5;45mos\x1b[0m    \x1b[38;5;147mPDA OS\x1b[0m");
            printLine("\x1b[38;5;51mhost\x1b[0m  \x1b[38;5;118mCardputer S3\x1b[0m");
            printLine("\x1b[38;5;220mcpu\x1b[0m   \x1b[38;5;252mESP32-S3 @240\x1b[0m");
            printLine("\x1b[38;5;220mmem\x1b[0m   \x1b[38;5;252m320KB / 8MB\x1b[0m");
            printLine("\x1b[38;5;220mdisp\x1b[0m  \x1b[38;5;252m240x135 TFT\x1b[0m");
            printLine("\x1b[38;5;220mui\x1b[0m    \x1b[38;5;252mLVGL9 + WiFi\x1b[0m");
        } else if (strcmp(_input_buf, "demo") == 0) {
            printLine("\x1b[38;5;196mred\x1b[0m \x1b[38;5;46mgreen\x1b[0m \x1b[38;5;27mblue\x1b[0m");
            printLine("\x1b[38;5;226myellow\x1b[0m \x1b[38;5;201mpink\x1b[0m \x1b[38;5;51mcyan\x1b[0m");
            printLine("\x1b[48;5;236;38;5;15m bg test \x1b[0m \x1b[48;5;52;38;5;231m ansi \x1b[0m");
            printLine("\x1b[38;5;208m256-color\x1b[0m \x1b[38;5;141mdemo\x1b[0m ready");
        } else if (strncmp(_input_buf, "echo ", 5) == 0) {
            printLine(_input_buf + 5);
        } else {
            static char buf[160];
            snprintf(buf, sizeof(buf), "unknown: %s", _input_buf);
            printLine(buf);
        }
        clearInput();
        return;
    }

    // UART/TELNET/SSH: send with newline
    backendSend(_input_buf, _input_len);
    clearInput();
}

void TerminalApp::handleBackspace() {
    if (_input_len > 0) {
        _input_buf[--_input_len] = '\0';
        updateInputDisplay();
    }
}

void TerminalApp::handleChar(char c) {
    if (_input_len < INPUT_MAX - 1) {
        _input_buf[_input_len++] = c;
        _input_buf[_input_len] = '\0';
        updateInputDisplay();
    }
}

void TerminalApp::clearInput() {
    _input_len = 0;
    _input_buf[0] = '\0';
    updateInputDisplay();
}

void TerminalApp::updateCursor() {
    if (!_input_label) return;
    static char cur_buf[INPUT_MAX + 8];
    strcpy(cur_buf, _input_buf);
    if (_cursor_visible && _input_len < INPUT_MAX - 1) {
        strcat(cur_buf, "_");
    }
    lv_textarea_set_text(_input_label, cur_buf);
    lv_textarea_set_cursor_pos(_input_label, LV_TEXTAREA_CURSOR_LAST);
}

void TerminalApp::updateInputDisplay() {
    _cursor_visible = true;
    _cursor_timer = millis();
    updateCursor();
}

void TerminalApp::updateStatus() {
    switch (_mode) {
        case TerminalMode::ECHO:
            lv_label_set_text(_mode_label, "[ECHO]");
            lv_obj_set_style_text_color(_mode_label, lv_color_make(0xff, 0xaa, 0x00), 0);
            break;
        case TerminalMode::UART:
            lv_label_set_text(_mode_label, "[UART]");
            lv_obj_set_style_text_color(_mode_label, lv_color_make(0x55, 0xaa, 0xff), 0);
            break;
        case TerminalMode::TELNET:
            lv_label_set_text(_mode_label, "[TELNET]");
            lv_obj_set_style_text_color(_mode_label, lv_color_make(0x55, 0xff, 0xaa), 0);
            break;
        case TerminalMode::SSH:
            lv_label_set_text(_mode_label, _ssh_connected ? "[SSH]" : (_ssh_waiting_password ? "[SSH:PASS]" : "[SSH]"));
            lv_obj_set_style_text_color(_mode_label, lv_color_make(0xaa, 0xff, 0xff), 0);
            break;
    }
}

void TerminalApp::switchMode() {
    switch (_mode) {
        case TerminalMode::ECHO:  setMode(TerminalMode::UART);    break;
        case TerminalMode::UART:   setMode(TerminalMode::TELNET);  break;
        case TerminalMode::TELNET: setMode(TerminalMode::SSH);   break;
        case TerminalMode::SSH:   setMode(TerminalMode::ECHO);   break;
    }
}

void TerminalApp::setMode(TerminalMode m) {
    backendClose();
    _mode = m;
    backendInit();
    updateStatus();
}

// ============================================================
// Backend
// ============================================================

void TerminalApp::backendInit() {
    switch (_mode) {
        case TerminalMode::UART: {
            Serial1.begin(115200, SERIAL_8N1, 13, 14);
            _serial_inited = true;
            printLine("UART: 115200 8N1, TX=13, RX=14");
            break;
        }
        case TerminalMode::TELNET: {
            printLine("TELNET mode");
            printLine("  connect <host> [port]");
            printLine("  disconnect");
            _net_connected = false;
            break;
        }
        case TerminalMode::SSH: {
            printLine("SSH mode");
            printLine("  connect <host> <user> [port]");
            printLine("  disconnect");
            _ssh_waiting_password = false;
            _ssh_connected = false;
            break;
        }
        case TerminalMode::ECHO:
        default:
            break;
    }
}

void TerminalApp::backendLoop() {
    // RX handled in onUpdate via putChar()
}

void TerminalApp::backendSend(const char* str, int len) {
    switch (_mode) {
        case TerminalMode::UART:
            if (_serial_inited) Serial1.write((const uint8_t*)str, len);
            break;
        case TerminalMode::TELNET:
            if (_net_connected && _telnet_client.connected()) {
                static char cmd[INPUT_MAX + 4];
                int n = len < (int)sizeof(cmd) - 2 ? len : sizeof(cmd) - 2;
                memcpy(cmd, str, n);
                cmd[n] = '\r'; cmd[n+1] = '\n';
                _telnet_client.write((const uint8_t*)cmd, n + 2);
            } else {
                printLine("[TELNET] Not connected");
            }
            break;
        case TerminalMode::SSH:
            if (_ssh_connected && _ssh_channel) {
                ssh_channel_write(_ssh_channel, str, len);
                ssh_channel_write(_ssh_channel, "\n", 1);
            } else {
                printLine("[SSH] Not connected");
            }
            break;
        case TerminalMode::ECHO:
        default:
            break;
    }
}

void TerminalApp::backendClose() {
    switch (_mode) {
        case TerminalMode::UART:
            if (_serial_inited) { Serial1.end(); _serial_inited = false; }
            break;
        case TerminalMode::TELNET:
            telnetDisconnect();
            break;
        case TerminalMode::SSH:
            sshDisconnect();
            break;
        default:
            break;
    }
}

bool TerminalApp::backendConnected() {
    switch (_mode) {
        case TerminalMode::UART:   return _serial_inited;
        case TerminalMode::TELNET: return _net_connected;
        case TerminalMode::SSH:    return _ssh_connected;
        default:                   return false;
    }
}

void TerminalApp::telnetConnect(const char* host, uint16_t port) {
    telnetDisconnect();

    static char info[96];
    snprintf(info, sizeof(info), "Connecting to %s:%d...", host, port);
    printLine(info);

    if (_telnet_client.connect(host, port)) {
        strncpy(_telnet_host, host, sizeof(_telnet_host) - 1);
        _telnet_host[sizeof(_telnet_host) - 1] = '\0';
        _telnet_port = port;
        _net_connected = true;
        printLine("Connected!");
    } else {
        printLine("Connection failed!");
    }
}

void TerminalApp::telnetDisconnect() {
    if (_telnet_client) _telnet_client.stop();
    _net_connected = false;
    _telnet_host[0] = '\0';
}

void TerminalApp::sshConnect(const char* host, const char* user, uint16_t port, const char* password) {
    sshDisconnect();
    _ssh_waiting_password = false;

    printLine("[SSH] Connecting...");

    _ssh_session = ssh_new();
    if (!_ssh_session) {
        printLine("[SSH] ssh_new failed");
        updateStatus();
        return;
    }

    int verbosity = SSH_LOG_NOLOG;
    unsigned int ssh_port = port;
    ssh_options_set(_ssh_session, SSH_OPTIONS_HOST, host);
    ssh_options_set(_ssh_session, SSH_OPTIONS_USER, user);
    ssh_options_set(_ssh_session, SSH_OPTIONS_PORT, &ssh_port);
    ssh_options_set(_ssh_session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

    if (ssh_connect(_ssh_session) != SSH_OK) {
        static char err[160];
        snprintf(err, sizeof(err), "[SSH] Connect failed: %s", ssh_get_error(_ssh_session));
        printLine(err);
        sshDisconnect();
        updateStatus();
        return;
    }

    int auth = ssh_userauth_password(_ssh_session, user, password);
    if (auth != SSH_AUTH_SUCCESS) {
        static char err[160];
        snprintf(err, sizeof(err), "[SSH] Auth failed: %s", ssh_get_error(_ssh_session));
        printLine(err);
        sshDisconnect();
        updateStatus();
        return;
    }

    _ssh_channel = ssh_channel_new(_ssh_session);
    if (!_ssh_channel) {
        printLine("[SSH] Channel alloc failed");
        sshDisconnect();
        updateStatus();
        return;
    }

    if (ssh_channel_open_session(_ssh_channel) != SSH_OK ||
        ssh_channel_request_pty_size(_ssh_channel, "xterm", 80, 24) != SSH_OK ||
        ssh_channel_request_shell(_ssh_channel) != SSH_OK) {
        static char err[160];
        snprintf(err, sizeof(err), "[SSH] Shell failed: %s", ssh_get_error(_ssh_session));
        printLine(err);
        sshDisconnect();
        updateStatus();
        return;
    }

    ssh_channel_set_blocking(_ssh_channel, 0);
    _ssh_connected = true;
    strncpy(_ssh_host, host, sizeof(_ssh_host) - 1);
    strncpy(_ssh_user, user, sizeof(_ssh_user) - 1);
    _ssh_port = port;
    printLine("[SSH] Connected");
    updateStatus();
}

void TerminalApp::sshDisconnect() {
    _ssh_waiting_password = false;
    if (_ssh_channel) {
        ssh_channel_send_eof(_ssh_channel);
        ssh_channel_close(_ssh_channel);
        ssh_channel_free(_ssh_channel);
        _ssh_channel = nullptr;
    }
    if (_ssh_session) {
        ssh_disconnect(_ssh_session);
        ssh_free(_ssh_session);
        _ssh_session = nullptr;
    }
    _ssh_connected = false;
    _ssh_host[0] = '\0';
    _ssh_user[0] = '\0';
    updateStatus();
}
