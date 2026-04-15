#include "settings_app.h"
#include <ui.h>
#include <lvgl.h>
#include <Arduino.h>
#include <M5Cardputer.h>
#include <config_manager.h>
#include <wifi_manager.h>
#include <WiFi.h>

extern const lv_font_t ui_font_noto_12;

static constexpr int TONE_NAV   = 4000;
static constexpr int TONE_OK    = 4500;
static constexpr int TONE_BACK  = 3000;
static constexpr int TONE_MS    = 20;

static void playTone(int freq) {
    if (atoi(ConfigManager::getVolume()) > 0) {
        M5.Speaker.tone(freq, TONE_MS);
    }
}

static bool isEditable(int sel) {
    return sel == 0 || sel == 1 || sel == 2 || sel == 3 || sel == 5;
}

static constexpr int16_t ENTRANCE_OFFSET = 50;
static constexpr float   STAGGER_GAP     = 0.10f;
static constexpr float   ENTRANCE_DUR    = 0.6f;
static constexpr float   SEL_BOUNCE_PX   = 6.0f;

static void mapContainers(lv_obj_t* c[], int n) {
    (void)n;
    c[0] = objects.settings_container_devicename;
    c[1] = objects.settings_container_brightness;
    c[2] = objects.settings_container_tone;
    c[3] = objects.settings_container_tformat;
    c[4] = objects.settings_container_tzone;
    c[5] = objects.settings_container_ssid;
    c[6] = objects.settings_container_support;
}

void SettingsApp::wifiEnterScan() {
    wifiClose();
    _wifi_state = 10;
    _ap_count = 0;
    _ap_sel = 0;
    _pwd_len = 0;
    _pwd[0] = '\0';

    // Use objects.settings as parent (the actual Settings screen), not lv_scr_act()
    lv_obj_t* parent = objects.settings ? objects.settings : lv_scr_act();
    Serial.printf("[WIFI] EnterScan parent=%p settings=%p scr=%p\n", parent, objects.settings, lv_scr_act());

    _wifi_screen = lv_obj_create(parent);
    lv_obj_set_size(_wifi_screen, 240, 135);
    lv_obj_set_pos(_wifi_screen, 0, 0);
    lv_obj_set_style_bg_color(_wifi_screen, lv_color_hex(0x0a0a15), 0);
    lv_obj_set_style_border_width(_wifi_screen, 0, 0);
    lv_obj_set_scrollbar_mode(_wifi_screen, LV_SCROLLBAR_MODE_OFF);

    _wifi_status = lv_label_create(_wifi_screen);
    lv_label_set_text(_wifi_status, "Scanning...");
    lv_obj_set_style_text_color(_wifi_status, lv_color_hex(0xffaa00), 0);
    lv_obj_set_style_text_font(_wifi_status, &ui_font_noto_12, 0);
    lv_obj_set_pos(_wifi_status, 8, 6);

    _wifi_pwd_label = nullptr;
    _wifi_list = nullptr;
}

void SettingsApp::wifiPollScan() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    if (n < 0) n = 0;

    _ap_count = 0;
    for (int i = 0; i < n && _ap_count < MAX_AP; ++i) {
        String s = WiFi.SSID(i);
        if (s.length() == 0) continue;

        bool dup = false;
        for (int j = 0; j < _ap_count; ++j) {
            if (strcmp(_aps[j].ssid, s.c_str()) == 0) {
                if (WiFi.RSSI(i) > _aps[j].rssi) {
                    _aps[j].rssi = WiFi.RSSI(i);
                    _aps[j].enc = WiFi.encryptionType(i);
                }
                dup = true;
                break;
            }
        }
        if (!dup) {
            strncpy(_aps[_ap_count].ssid, s.c_str(), sizeof(_aps[_ap_count].ssid) - 1);
            _aps[_ap_count].ssid[sizeof(_aps[_ap_count].ssid) - 1] = '\0';
            _aps[_ap_count].rssi = WiFi.RSSI(i);
            _aps[_ap_count].enc = WiFi.encryptionType(i);
            _ap_count++;
        }
    }

    for (int i = 0; i < _ap_count - 1; ++i) {
        for (int j = i + 1; j < _ap_count; ++j) {
            if (_aps[j].rssi > _aps[i].rssi) {
                WifiAP tmp = _aps[i];
                _aps[i] = _aps[j];
                _aps[j] = tmp;
            }
        }
    }

    WiFi.scanDelete();
    wifiBuildList();
}

void SettingsApp::updateWifiSelectionStyles() {
    if (!_wifi_list) return;
    uint32_t count = lv_obj_get_child_cnt(_wifi_list);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* btn = lv_obj_get_child(_wifi_list, i);
        if (!btn) continue;
        lv_obj_set_style_bg_color(btn,
            ((int)i == _ap_sel) ? lv_color_hex(0x335577) : lv_color_hex(0x1a1a2e), 0);
    }
}

void SettingsApp::wifiBuildList() {
    if (!_wifi_screen) return;

    // Delete old children one by one (not lv_obj_clean which may cause issues)
    if (_wifi_status) { lv_obj_del(_wifi_status); _wifi_status = nullptr; }
    if (_wifi_list) { lv_obj_del(_wifi_list); _wifi_list = nullptr; }
    if (_wifi_pwd_label) { lv_obj_del(_wifi_pwd_label); _wifi_pwd_label = nullptr; }

    Serial.printf("[WIFI] BuildList: %d APs, screen=%p\n", _ap_count, _wifi_screen);

    if (_ap_count == 0) {
        _wifi_status = lv_label_create(_wifi_screen);
        lv_label_set_text(_wifi_status, "No networks found");
        lv_obj_set_style_text_color(_wifi_status, lv_color_hex(0xff5555), 0);
        lv_obj_set_style_text_font(_wifi_status, &ui_font_noto_12, 0);
        lv_obj_set_pos(_wifi_status, 8, 6);
        _wifi_state = 2;
        return;
    }

    _wifi_status = lv_label_create(_wifi_screen);
    lv_label_set_text(_wifi_status, "Select WiFi");
    lv_obj_set_style_text_color(_wifi_status, lv_color_hex(0x55aaff), 0);
    lv_obj_set_style_text_font(_wifi_status, &ui_font_noto_12, 0);
    lv_obj_set_pos(_wifi_status, 8, 6);

    _wifi_list = lv_list_create(_wifi_screen);
    lv_obj_set_size(_wifi_list, 224, 100);
    lv_obj_set_pos(_wifi_list, 8, 28);
    lv_obj_set_style_bg_color(_wifi_list, lv_color_hex(0x0a0a15), 0);
    lv_obj_set_style_border_width(_wifi_list, 0, 0);
    lv_obj_set_style_text_color(_wifi_list, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(_wifi_list, &ui_font_noto_12, 0);

    for (int i = 0; i < _ap_count; ++i) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%c %-20s %d",
                 _aps[i].enc ? '*' : 'o', _aps[i].ssid, _aps[i].rssi);
        lv_obj_t* btn = lv_list_add_btn(_wifi_list, NULL, buf);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a1a2e), 0);
    }

    _wifi_state = 2;
    updateWifiSelectionStyles();
}

void SettingsApp::wifiEnterPassword() {
    if (!_wifi_screen) return;
    _wifi_state = 3;
    _pwd_len = 0;
    _pwd[0] = '\0';

    lv_obj_clean(_wifi_screen);
    _wifi_list = nullptr;

    _wifi_status = lv_label_create(_wifi_screen);
    char ssidBuf[64];
    snprintf(ssidBuf, sizeof(ssidBuf), "SSID: %s", _aps[_ap_sel].ssid);
    lv_label_set_text(_wifi_status, ssidBuf);
    lv_obj_set_style_text_color(_wifi_status, lv_color_hex(0x55aaff), 0);
    lv_obj_set_style_text_font(_wifi_status, &ui_font_noto_12, 0);
    lv_obj_set_pos(_wifi_status, 8, 6);

    _wifi_pwd_label = lv_label_create(_wifi_screen);
    lv_obj_set_pos(_wifi_pwd_label, 8, 40);
    lv_obj_set_style_text_color(_wifi_pwd_label, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_text_font(_wifi_pwd_label, &ui_font_noto_12, 0);
    lv_label_set_text(_wifi_pwd_label, "_");

    _wifi_list = lv_label_create(_wifi_screen);
    lv_obj_set_pos(_wifi_list, 8, 78);
    lv_obj_set_style_text_color(_wifi_list, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(_wifi_list, &ui_font_noto_12, 0);
    lv_label_set_text(_wifi_list, "Enter: connect | `: back");
}

void SettingsApp::wifiUpdatePwdDisplay() {
    if (!_wifi_pwd_label) return;
    char dots[66];
    for (int i = 0; i < _pwd_len; ++i) dots[i] = '*';
    dots[_pwd_len] = '_';
    dots[_pwd_len + 1] = '\0';
    lv_label_set_text(_wifi_pwd_label, dots);
}

void SettingsApp::wifiConnect() {
    _wifi_state = 4;
    _pwd[_pwd_len] = '\0';
    ConfigManager::set("wifi_ssid", _aps[_ap_sel].ssid);
    ConfigManager::set("wifi_password", _pwd);

    if (_wifi_pwd_label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Connecting to %s...", _aps[_ap_sel].ssid);
        lv_label_set_text(_wifi_pwd_label, buf);
    }

    WiFi.begin(_aps[_ap_sel].ssid, _pwd);
    _wifi_connect_start = millis();
}

void SettingsApp::wifiClose() {
    if (_wifi_screen) {
        lv_obj_del(_wifi_screen);
        _wifi_screen = nullptr;
    }
    _wifi_state = 0;
    _wifi_status = nullptr;
    _wifi_list = nullptr;
    _wifi_pwd_label = nullptr;
}

void SettingsApp::onCreate() {
    _sel = 0;
    _prev_sel = 0;
    _editing = false;
    _on_support_screen = false;
    _wifi_state = 0;

    if (!objects.settings) create_screen_settings();
    loadScreen(SCREEN_ID_SETTINGS);
    mapContainers(_containers, MENU_COUNT);

    for (int i = 0; i < MENU_COUNT; ++i) {
        if (_containers[i]) {
            lv_obj_set_style_translate_x(_containers[i], ENTRANCE_OFFSET, 0);
            lv_obj_set_style_opa(_containers[i], 0, 0);
        }
    }

    loadValues();
    updateSelection();

    _entrance.start = 0.0f;
    _entrance.end = 1.0f;
    _entrance.delay = 0.0f;
    _entrance.easingOptions().duration = ENTRANCE_DUR;
    _entrance.easingOptions().easingFunction = ease::ease_out_back;
    _entrance.init();
    _entrance.play();
    _entering = true;

    _sel_bounce.springOptions().stiffness = 300;
    _sel_bounce.springOptions().damping = 12;
    _sel_bounce.springOptions().mass = 0.8f;
    _sel_bounce.springOptions().bounce = 0.6f;
    _sel_bounce.begin();
    _sel_bounce.teleport(0.0f);
}

void SettingsApp::onUpdate() {
    if (_wifi_state == 10) {
        WiFi.mode(WIFI_STA);
        _wifi_state = 11;
    } else if (_wifi_state == 11) {
        WiFi.scanNetworks(true);
        _wifi_state = 1;
    } else if (_wifi_state == 1) {
        wifiPollScan();
    } else if (_wifi_state == 4) {
        wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) {
            if (_wifi_pwd_label) lv_label_set_text(_wifi_pwd_label, "Connected!");
            loadValues();
            _wifi_state = 5;
        } else if (millis() - _wifi_connect_start > 10000) {
            if (_wifi_pwd_label) lv_label_set_text(_wifi_pwd_label, "Failed. Press ` to close.");
            _wifi_state = 5;
        }
    }

    if (_entering) {
        _entrance.update();
        float p = _entrance.value();
        float range = 1.0f - (MENU_COUNT - 1) * STAGGER_GAP;
        for (int i = 0; i < MENU_COUNT; ++i) {
            if (!_containers[i]) continue;
            float cp = (p - i * STAGGER_GAP) / range;
            if (cp < 0.0f) cp = 0.0f;
            if (cp > 1.0f) cp = 1.0f;
            lv_opa_t target_opa = (i == _sel) ? 255 : 150;
            lv_obj_set_style_translate_x(_containers[i], (int16_t)((1.0f - cp) * ENTRANCE_OFFSET), 0);
            lv_obj_set_style_opa(_containers[i], (lv_opa_t)(cp * target_opa), 0);
        }
        if (_entrance.done()) {
            _entering = false;
            for (int i = 0; i < MENU_COUNT; ++i) {
                if (!_containers[i]) continue;
                lv_obj_remove_local_style_prop(_containers[i], LV_STYLE_TRANSLATE_X, 0);
                lv_obj_remove_local_style_prop(_containers[i], LV_STYLE_OPA, 0);
            }
        }
    }

    if (!_entering && _wifi_state == 0) {
        float bx = _sel_bounce.value();
        for (int i = 0; i < MENU_COUNT; ++i) {
            if (!_containers[i]) continue;
            if (i == _sel) lv_obj_set_style_translate_x(_containers[i], (int16_t)bx, 0);
            else lv_obj_remove_local_style_prop(_containers[i], LV_STYLE_TRANSLATE_X, 0);
        }
    }
}

void SettingsApp::onDestroy() {
    _sel_bounce.stop();
    wifiClose();
    for (int i = 0; i < MENU_COUNT; ++i) {
        if (!_containers[i]) continue;
        lv_obj_remove_local_style_prop(_containers[i], LV_STYLE_TRANSLATE_X, 0);
        lv_obj_remove_local_style_prop(_containers[i], LV_STYLE_OPA, 0);
    }
}

void SettingsApp::loadValues() {
    if (objects.settings_textarea_devicename)
        lv_textarea_set_text(objects.settings_textarea_devicename, ConfigManager::getDeviceName());

    int b = atoi(ConfigManager::getBrightness());
    if (b < 25) b = 25; if (b > 255) b = 255;
    int bp = b * 100 / 255;
    if (objects.settings_slider_brightness) lv_bar_set_value(objects.settings_slider_brightness, bp, LV_ANIM_OFF);
    if (objects.settings_label_brightness) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", bp);
        lv_label_set_text(objects.settings_label_brightness, buf);
    }

    int v = atoi(ConfigManager::getVolume());
    if (v < 0) v = 0; if (v > 255) v = 255;
    int vp = v * 100 / 255;
    if (objects.settings_slider_tone) lv_bar_set_value(objects.settings_slider_tone, vp, LV_ANIM_OFF);
    if (objects.settings_label_tone) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", vp);
        lv_label_set_text(objects.settings_label_tone, buf);
    }

    if (objects.settings_label_ssid) {
        const char* ssid = ConfigManager::getWifiSSID();
        if (ssid[0]) {
            char buf[40];
            if (WifiManager::isConnected()) snprintf(buf, sizeof(buf), "%s ✓", ssid);
            else snprintf(buf, sizeof(buf), "%s", ssid);
            lv_label_set_text(objects.settings_label_ssid, buf);
        } else {
            lv_label_set_text(objects.settings_label_ssid, "Scan & connect");
        }
    }

    if (objects.settings_label_tzone) {
        const char* tz = ConfigManager::getTimezone();
        lv_label_set_text(objects.settings_label_tzone, tz[0] ? tz : "UTC0");
    }

    if (objects.settings_roller_tformat) {
        const char* fmt = ConfigManager::getTimeFormat();
        lv_roller_set_selected(objects.settings_roller_tformat, (strcmp(fmt, "24") == 0) ? 1 : 0, LV_ANIM_OFF);
    }
}

void SettingsApp::updateSelection() {
    for (int i = 0; i < MENU_COUNT; ++i) {
        if (!_containers[i]) continue;
        if (_editing && i == _sel) {
            lv_obj_add_state(_containers[i], LV_STATE_CHECKED);
            lv_obj_remove_state(_containers[i], (lv_state_t)LV_STATE_FOCUS_KEY);
        } else {
            lv_obj_remove_state(_containers[i], LV_STATE_CHECKED);
            if (i == _sel) lv_obj_add_state(_containers[i], LV_STATE_FOCUS_KEY);
            else lv_obj_remove_state(_containers[i], (lv_state_t)LV_STATE_FOCUS_KEY);
        }
    }
    if (_containers[_sel] && objects.settings) lv_obj_scroll_to_view(_containers[_sel], LV_ANIM_ON);
}

void SettingsApp::enterEdit() {
    if (_sel == 6) {
        _on_support_screen = true;
        if (!objects.support) create_screen_support();
        loadScreen(SCREEN_ID_SUPPORT);
        return;
    }
    if (!isEditable(_sel)) return;

    // WiFi is NOT normal edit mode - it's an independent overlay
    // Only start scan if not already running or completed
    if (_sel == 5) {
        if (_wifi_state == 0) {
            wifiEnterScan();
        }
        return;
    }

    _editing = true;
    updateSelection();

    if (_sel == 0 && objects.settings_textarea_devicename) {
        lv_obj_add_state(objects.settings_textarea_devicename, LV_STATE_FOCUSED);
        lv_obj_add_state(objects.settings_textarea_devicename, LV_STATE_CHECKED);
    }
    if (_sel == 1 && objects.settings_slider_brightness) lv_obj_add_state(objects.settings_slider_brightness, LV_STATE_CHECKED);
    if (_sel == 2 && objects.settings_slider_tone) lv_obj_add_state(objects.settings_slider_tone, LV_STATE_CHECKED);
    if (_sel == 3 && objects.settings_roller_tformat) lv_obj_add_state(objects.settings_roller_tformat, LV_STATE_CHECKED);
}

void SettingsApp::exitEdit(bool save) {
    if (!_editing) return;
    if (save) {
        switch (_sel) {
            case 0:
                if (objects.settings_textarea_devicename)
                    ConfigManager::set("device_name", lv_textarea_get_text(objects.settings_textarea_devicename));
                break;
            case 1:
                saveBrightness();
                break;
            case 2:
                if (objects.settings_slider_tone) {
                    int pct = lv_bar_get_value(objects.settings_slider_tone);
                    int vol = pct * 255 / 100;
                    M5.Speaker.setVolume(vol);
                    char vbuf[8]; snprintf(vbuf, sizeof(vbuf), "%d", vol);
                    ConfigManager::set("volume", vbuf);
                }
                break;
            case 3:
                if (objects.settings_roller_tformat) {
                    uint32_t s = lv_roller_get_selected(objects.settings_roller_tformat);
                    ConfigManager::set("time_format", s == 1 ? "24" : "12");
                }
                break;
            case 5:
                wifiClose();
                loadValues();
                break;
        }
    } else {
        loadValues();
        if (_sel == 1) {
            int bright = atoi(ConfigManager::getBrightness());
            if (bright < 25) bright = 25; if (bright > 255) bright = 255;
            M5.Display.setBrightness(bright);
        }
        if (_sel == 2) {
            int vol = atoi(ConfigManager::getVolume());
            if (vol < 0) vol = 0; if (vol > 255) vol = 255;
            M5.Speaker.setVolume(vol);
        }
    }

    _editing = false;
    if (objects.settings_textarea_devicename) {
        lv_obj_remove_state(objects.settings_textarea_devicename, LV_STATE_FOCUSED);
        lv_obj_remove_state(objects.settings_textarea_devicename, LV_STATE_CHECKED);
    }
    if (objects.settings_slider_brightness) lv_obj_remove_state(objects.settings_slider_brightness, LV_STATE_CHECKED);
    if (objects.settings_slider_tone) lv_obj_remove_state(objects.settings_slider_tone, LV_STATE_CHECKED);
    if (objects.settings_roller_tformat) lv_obj_remove_state(objects.settings_roller_tformat, LV_STATE_CHECKED);
    updateSelection();
}

void SettingsApp::saveBrightness() {
    if (!objects.settings_slider_brightness) return;
    int pct = lv_bar_get_value(objects.settings_slider_brightness);
    int bright = pct * 255 / 100;
    M5.Display.setBrightness(bright);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", bright);
    ConfigManager::set("brightness", buf);
    if (objects.settings_label_brightness) {
        snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(objects.settings_label_brightness, buf);
    }
}

void SettingsApp::handleEditKey(char key) {
    if (_sel == 5 && _wifi_state == 3) {
        if (key >= 32 && key < 127 && _pwd_len < 64) {
            _pwd[_pwd_len++] = key;
            _pwd[_pwd_len] = '\0';
            wifiUpdatePwdDisplay();
        } else if (key == 0x08 && _pwd_len > 0) {
            _pwd_len--;
            _pwd[_pwd_len] = '\0';
            wifiUpdatePwdDisplay();
        }
        return;
    }

    switch (_sel) {
        case 0:
            if (objects.settings_textarea_devicename) {
                if (key == 0x08) lv_textarea_delete_char(objects.settings_textarea_devicename);
                else if (key >= 32 && key < 127) {
                    char s[2] = {key, 0};
                    lv_textarea_add_text(objects.settings_textarea_devicename, s);
                }
            }
            break;
        case 1:
            if (objects.settings_slider_brightness) {
                int val = lv_bar_get_value(objects.settings_slider_brightness);
                if (key == ';') val += 10; else if (key == '.') val -= 10;
                if (val < 10) val = 10; if (val > 100) val = 100;
                lv_bar_set_value(objects.settings_slider_brightness, val, LV_ANIM_OFF);
                M5.Display.setBrightness(val * 255 / 100);
                if (objects.settings_label_brightness) {
                    char buf[8]; snprintf(buf, sizeof(buf), "%d%%", val);
                    lv_label_set_text(objects.settings_label_brightness, buf);
                }
            }
            break;
        case 2:
            if (objects.settings_slider_tone) {
                int val = lv_bar_get_value(objects.settings_slider_tone);
                if (key == ';') val += 10; else if (key == '.') val -= 10;
                if (val < 0) val = 0; if (val > 100) val = 100;
                lv_bar_set_value(objects.settings_slider_tone, val, LV_ANIM_OFF);
                M5.Speaker.setVolume(val * 255 / 100);
                if (objects.settings_label_tone) {
                    char buf[8]; snprintf(buf, sizeof(buf), "%d%%", val);
                    lv_label_set_text(objects.settings_label_tone, buf);
                }
            }
            break;
        case 3:
            if (objects.settings_roller_tformat) {
                uint32_t cnt = lv_roller_get_option_count(objects.settings_roller_tformat);
                uint32_t s = lv_roller_get_selected(objects.settings_roller_tformat);
                if (key == ';') s = (s + cnt - 1) % cnt;
                else if (key == '.') s = (s + 1) % cnt;
                lv_roller_set_selected(objects.settings_roller_tformat, s, LV_ANIM_ON);
            }
            break;
    }
}

void SettingsApp::onKeyPressed(char key) {
    // WiFi overlay handling - independent of _editing
    if (_sel == 5 && _wifi_state > 0) {
        if (key == '`') {
            playTone(TONE_BACK);
            wifiClose();
            _editing = false;
            updateSelection();
            return;
        }

        if (_wifi_state == 2 && _ap_count > 0) {
            if (key == ';' && _ap_sel > 0) {
                _ap_sel--;
                playTone(TONE_NAV);
                updateWifiSelectionStyles();
            } else if (key == '.' && _ap_sel < _ap_count - 1) {
                _ap_sel++;
                playTone(TONE_NAV);
                updateWifiSelectionStyles();
            } else if (key == '\n') {
                playTone(TONE_OK);
                if (_aps[_ap_sel].enc == 0) {
                    _pwd_len = 0;
                    _pwd[0] = '\0';
                    wifiConnect();
                } else {
                    wifiEnterPassword();
                }
            }
            return;
        }

        if (_wifi_state == 3) {
            if (key == '\n') {
                playTone(TONE_OK);
                wifiConnect();
            } else {
                playTone(TONE_NAV);
                handleEditKey(key);
            }
            return;
        }

        return;
    }

    if (_on_support_screen) {
        if (key == '`' || key == '\n') {
            playTone(TONE_BACK);
            _on_support_screen = false;
            loadScreen(SCREEN_ID_SETTINGS);
            mapContainers(_containers, MENU_COUNT);
            loadValues();
            updateSelection();
            for (int i = 0; i < MENU_COUNT; ++i) {
                if (_containers[i]) {
                    lv_obj_set_style_translate_x(_containers[i], ENTRANCE_OFFSET, 0);
                    lv_obj_set_style_opa(_containers[i], 0, 0);
                }
            }
            _entrance.init();
            _entrance.play();
            _entering = true;
        }
        return;
    }

    if (key == '`') {
        playTone(TONE_BACK);
        if (_editing) exitEdit(false);
        else if (_back_app) _manager.switchApp(_back_app);
        return;
    }

    if (_editing) {
        if (key == '\n') {
            playTone(TONE_OK);
            exitEdit(true);
        } else {
            playTone(TONE_NAV);
            handleEditKey(key);
        }
        return;
    }

    switch (key) {
        case ';':
            playTone(TONE_NAV);
            if (_containers[_sel]) lv_obj_remove_local_style_prop(_containers[_sel], LV_STYLE_TRANSLATE_X, 0);
            _prev_sel = _sel;
            _sel = (_sel - 1 + MENU_COUNT) % MENU_COUNT;
            updateSelection();
            _sel_bounce.teleport(SEL_BOUNCE_PX);
            _sel_bounce.move(0.0f);
            break;
        case '.':
            playTone(TONE_NAV);
            if (_containers[_sel]) lv_obj_remove_local_style_prop(_containers[_sel], LV_STYLE_TRANSLATE_X, 0);
            _prev_sel = _sel;
            _sel = (_sel + 1) % MENU_COUNT;
            updateSelection();
            _sel_bounce.teleport(SEL_BOUNCE_PX);
            _sel_bounce.move(0.0f);
            break;
        case '\n':
            playTone(TONE_OK);
            enterEdit();
            break;
    }
}
