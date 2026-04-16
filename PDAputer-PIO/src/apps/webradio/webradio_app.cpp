#include "webradio_app.h"
#include <ui.h>
#include <images.h>
#include <Arduino.h>
#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <MP3DecoderHelix.h>
#include <config_manager.h>
#if __has_include(<driver/i2s_std.h>)
#include <driver/i2s_std.h>
#else
#include <driver/i2s.h>
#endif

#define I2S_DOUT_PIN 42
#define I2S_BCLK_PIN 41
#define I2S_LRCK_PIN 43

#if !__has_include(<driver/i2s_std.h>)
static inline void i2s_del_channel(i2s_chan_handle_t chan) {
    if (chan) i2s_driver_uninstall(*chan);
}
static inline esp_err_t i2s_channel_disable(i2s_chan_handle_t chan) {
    return chan ? i2s_stop(*chan) : ESP_OK;
}
static inline esp_err_t i2s_channel_enable(i2s_chan_handle_t chan) {
    return chan ? i2s_start(*chan) : ESP_OK;
}
static inline esp_err_t i2s_channel_write(i2s_chan_handle_t chan, const void* src, size_t size, size_t* bytes_written, TickType_t ticks_to_wait) {
    return chan ? i2s_write(*chan, src, size, bytes_written, ticks_to_wait) : ESP_FAIL;
}
#endif

static const WebRadioStation STATIONS[] = {
    { "\xe4\xb8\xad\xe5\x9b\xbd\xe4\xb9\x8b\xe5\xa3\xb0",           "https://ngcdn001.cnr.cn/live/zgzs/index.m3u8" },
    { "\xe7\xbb\x8f\xe6\xb5\x8e\xe4\xb9\x8b\xe5\xa3\xb0",           "https://ngcdn002.cnr.cn/live/jjzs/index.m3u8" },
    { "\xe9\x9f\xb3\xe4\xb9\x90\xe4\xb9\x8b\xe5\xa3\xb0",           "https://ngcdn003.cnr.cn/live/yyzs/index.m3u8" },
    { "\xe7\xbb\x8f\xe5\x85\xb8\xe9\x9f\xb3\xe4\xb9\x90\xe5\xb9\xbf\xe6\x92\xad", "https://ngcdn004.cnr.cn/live/dszs/index.m3u8" },
    { "\xe5\x8f\xb0\xe6\xb9\xbe\xe4\xb9\x8b\xe5\xa3\xb0",           "https://ngcdn005.cnr.cn/live/zhzs/index.m3u8" },
    { "\xe7\xa5\x9e\xe5\xb7\x9e\xe4\xb9\x8b\xe5\xa3\xb0",           "https://ngcdn006.cnr.cn/live/szzs/index.m3u8" },
    { "\xe5\xa4\xa7\xe6\xb9\xbe\xe5\x8c\xba\xe4\xb9\x8b\xe5\xa3\xb0",         "https://ngcdn007.cnr.cn/live/hxzs/index.m3u8" },
    { "\xe6\xb0\x91\xe6\x97\x8f\xe4\xb9\x8b\xe5\xa3\xb0",           "https://ngcdn009.cnr.cn/live/mzzs/index.m3u8" },
    { "\xe6\x96\x87\xe8\x89\xba\xe4\xb9\x8b\xe5\xa3\xb0",           "https://ngcdn010.cnr.cn/live/wyzs/index.m3u8" },
    { "\xe8\x80\x81\xe5\xb9\xb4\xe4\xb9\x8b\xe5\xa3\xb0",           "https://ngcdn011.cnr.cn/live/lnzs/index.m3u8" },
    { "\xe8\x97\x8f\xe8\xaf\xad\xe5\xb9\xbf\xe6\x92\xad",           "https://ngcdn012.cnr.cn/live/zygb/index.m3u8" },
    { "\xe9\x98\x85\xe8\xaf\xbb\xe4\xb9\x8b\xe5\xa3\xb0",           "https://ngcdn014.cnr.cn/live/ylgb/index.m3u8" },
    { "\xe9\xa6\x99\xe6\xb8\xaf\xe4\xb9\x8b\xe5\xa3\xb0",           "https://ngcdn008.cnr.cn/live/xgzs/index.m3u8" },
    { "\xe4\xb8\xad\xe5\x9b\xbd\xe4\xba\xa4\xe9\x80\x9a\xe5\xb9\xbf\xe6\x92\xad", "https://ngcdn016.cnr.cn/live/gsgljtgb/index.m3u8" },
    { "\xe4\xb8\xad\xe5\x9b\xbd\xe4\xb9\xa1\xe6\x9d\x91\xe5\xb9\xbf\xe6\x92\xad", "https://ngcdn017.cnr.cn/live/xczs/index.m3u8" },
    { "\xe5\x8d\x8e\xe8\xaf\xad\xe5\xb9\xbf\xe6\x92\xad",           "https://ngcdn025.cnr.cn/live/hygb/index.m3u8" },
};

static libhelix::MP3DecoderHelix* s_mp3 = nullptr;
static i2s_chan_handle_t s_i2s_tx = nullptr;
static int16_t s_volume = 16;
static volatile bool s_first_frame = true;

static void mp3DataCb(MP3FrameInfo &info, short *pcm, size_t len, void *ref) {
    if (len == 0 || !s_i2s_tx) return;

    if (s_first_frame) {
#if __has_include(<driver/i2s_std.h>)
        i2s_channel_disable(s_i2s_tx);
        i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(info.samprate);
        i2s_channel_reconfig_std_clock(s_i2s_tx, &clk);
        i2s_channel_enable(s_i2s_tx);
#else
        if (s_i2s_tx) i2s_set_sample_rates(*s_i2s_tx, info.samprate);
#endif
        s_first_frame = false;
        Serial.printf("[WEBRADIO] MP3: %dHz %dch %dkbps\n",
                      info.samprate, info.nChans, info.bitrate / 1000);
    }

    for (size_t i = 0; i < len; i++) {
        pcm[i] = (int32_t)pcm[i] * s_volume / 21;
    }

    size_t bytes_written;
    i2s_channel_write(s_i2s_tx, pcm, len * sizeof(short), &bytes_written, pdMS_TO_TICKS(100));
}

void WebRadioApp::setupI2S(uint32_t sample_rate, bool stereo) {
    teardownI2S();

#if __has_include(<driver/i2s_std.h>)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;
    i2s_new_channel(&chan_cfg, &_i2s_tx, NULL);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_BCLK_PIN,
            .ws = (gpio_num_t)I2S_LRCK_PIN,
            .dout = (gpio_num_t)I2S_DOUT_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    i2s_channel_init_std_mode(_i2s_tx, &std_cfg);
    i2s_channel_enable(_i2s_tx);
#else
    static i2s_port_t port = I2S_NUM_0;
    _i2s_tx = &port;

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = sample_rate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = stereo ? I2S_CHANNEL_FMT_RIGHT_LEFT : I2S_CHANNEL_FMT_ONLY_RIGHT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = 0;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = I2S_BCLK_PIN;
    pins.ws_io_num = I2S_LRCK_PIN;
    pins.data_out_num = I2S_DOUT_PIN;
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    i2s_driver_install(port, &cfg, 0, NULL);
    i2s_set_pin(port, &pins);
    i2s_zero_dma_buffer(port);
    i2s_start(port);
#endif
    _i2s_running = true;
}

void WebRadioApp::teardownI2S() {
    if (_i2s_tx) {
        if (_i2s_running) {
            i2s_channel_disable(_i2s_tx);
            _i2s_running = false;
        }
        i2s_del_channel(_i2s_tx);
        _i2s_tx = nullptr;
    }
}

int WebRadioApp::ringWrite(const uint8_t* data, int len) {
    int written = 0;
    for (int i = 0; i < len; i++) {
        if (_ring_count >= RING_BUF_SIZE) break;
        _ring_buf[_ring_head] = data[i];
        _ring_head = (_ring_head + 1) % RING_BUF_SIZE;
        _ring_count++;
        written++;
    }
    return written;
}

int WebRadioApp::ringRead(uint8_t* data, int len) {
    int read = 0;
    for (int i = 0; i < len; i++) {
        if (_ring_count <= 0) break;
        data[i] = _ring_buf[_ring_tail];
        _ring_tail = (_ring_tail + 1) % RING_BUF_SIZE;
        _ring_count--;
        read++;
    }
    return read;
}

static String parseM3U8(WiFiClient& client, const String& baseUrl) {
    String line;
    String streamUrl;
    bool found = false;

    unsigned long startMs = millis();
    while ((millis() - startMs < 5000) && (client.available() || client.connected())) {
        line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        if (line.startsWith("#")) continue;

        if (line.startsWith("http")) {
            streamUrl = line;
        } else {
            int lastSlash = baseUrl.lastIndexOf('/');
            if (lastSlash >= 0) {
                streamUrl = baseUrl.substring(0, lastSlash + 1) + line;
            } else {
                streamUrl = line;
            }
        }
        found = true;
        break;
    }

    client.stop();
    if (!found) return "";
    return streamUrl;
}

static String resolveStreamUrl(const char* m3u8Url) {
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(5000);

    if (!http.begin(m3u8Url)) return "";

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return "";
    }

    WiFiClient* stream = http.getStreamPtr();
    String baseUrl = String(m3u8Url);
    String result = parseM3U8(*stream, baseUrl);
    http.end();

    if (result.length() == 0) return "";

    if (result.endsWith(".m3u8") || result.indexOf(".m3u8?") >= 0) {
        HTTPClient http2;
        http2.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http2.setTimeout(5000);

        if (!http2.begin(result)) return "";

        int code2 = http2.GET();
        if (code2 != HTTP_CODE_OK) {
            http2.end();
            return "";
        }

        WiFiClient* stream2 = http2.getStreamPtr();
        String result2 = parseM3U8(*stream2, result);
        http2.end();
        return result2;
    }

    return result;
}

void WebRadioApp::netTaskFunc(void* param) {
    WebRadioApp* self = (WebRadioApp*)param;

    while (self->_net_task_run) {
        if (!self->_connecting) {
            vTaskDelay(50);
            continue;
        }

        const char* m3u8Url = STATIONS[self->_selected].url;
        Serial.printf("[WEBRADIO] Resolving: %s\n", m3u8Url);

        String streamUrl = resolveStreamUrl(m3u8Url);

        if (streamUrl.length() == 0 || self->_stop_requested) {
            Serial.println("[WEBRADIO] Failed to resolve stream URL");
            self->_stream_error = true;
            self->_connecting = false;
            continue;
        }

        Serial.printf("[WEBRADIO] Stream URL: %s\n", streamUrl.c_str());

        HTTPClient http;
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(10000);

        if (!http.begin(streamUrl)) {
            Serial.println("[WEBRADIO] HTTP begin failed");
            self->_stream_error = true;
            self->_connecting = false;
            continue;
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            Serial.printf("[WEBRADIO] HTTP GET failed: %d\n", code);
            http.end();
            self->_stream_error = true;
            self->_connecting = false;
            continue;
        }

        self->_connecting = false;
        self->_playing = true;
        Serial.println("[WEBRADIO] Streaming started");

        WiFiClient* stream = http.getStreamPtr();
        uint8_t netBuf[2048];

        while (self->_net_task_run && !self->_stop_requested && stream->connected()) {
            int avail = stream->available();
            if (avail <= 0) {
                vTaskDelay(5);
                continue;
            }

            int toRead = avail < (int)sizeof(netBuf) ? avail : (int)sizeof(netBuf);
            int rd = stream->read(netBuf, toRead);
            if (rd > 0) {
                int totalWritten = 0;
                while (totalWritten < rd && self->_net_task_run && !self->_stop_requested) {
                    int w = self->ringWrite(netBuf + totalWritten, rd - totalWritten);
                    totalWritten += w;
                    if (w == 0) vTaskDelay(5);
                }
            }
        }

        http.end();
        self->_playing = false;
        Serial.println("[WEBRADIO] Stream ended");

        while (self->_ring_count > 0 && self->_audio_task_run) {
            vTaskDelay(10);
        }
    }

    vTaskDelete(NULL);
}

void WebRadioApp::audioTaskFunc(void* param) {
    WebRadioApp* self = (WebRadioApp*)param;
    uint8_t buf[1024];

    while (self->_audio_task_run) {
        if (self->_ring_count < 1024) {
            vTaskDelay(10);
            continue;
        }

        int rd = self->ringRead(buf, sizeof(buf));
        if (rd > 0 && s_mp3) {
            s_mp3->write(buf, rd);
        }

        vTaskDelay(1);
    }

    vTaskDelete(NULL);
}

void WebRadioApp::timerCb(lv_timer_t* t) {
    WebRadioApp* self = (WebRadioApp*)lv_timer_get_user_data(t);
    if (self) self->updateStatusUI();
}

void WebRadioApp::flashControl(lv_obj_t* obj) {
    if (!obj) return;
    lv_obj_add_state(obj, LV_STATE_FOCUS_KEY);
    lv_timer_create([](lv_timer_t* t) {
        lv_obj_t* o = (lv_obj_t*)lv_timer_get_user_data(t);
        if (o) lv_obj_remove_state(o, LV_STATE_FOCUS_KEY);
        lv_timer_delete(t);
    }, 150, obj);
}

void WebRadioApp::onCreate() {
    _station_count = sizeof(STATIONS) / sizeof(STATIONS[0]);
    _ring_buf = (uint8_t*)heap_caps_malloc(RING_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_ring_buf) {
        _ring_buf = (uint8_t*)malloc(RING_BUF_SIZE);
    }
    if (!_ring_buf) {
        Serial.println("[WEBRADIO] Failed to allocate ring buffer!");
        return;
    }
    _ring_head = 0;
    _ring_tail = 0;
    _ring_count = 0;

    delay(30);
    M5.Speaker.stop();
    M5.Speaker.end();

    s_volume = _volume;

    _screen = lv_obj_create(NULL);
    lv_obj_set_size(_screen, 240, 135);
    lv_obj_remove_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(0xff191919), LV_PART_MAIN);

    {
        lv_obj_t* parent = _screen;

        {
            _label_station = lv_label_create(parent);
            lv_obj_set_pos(_label_station, 11, 5);
            lv_obj_set_size(_label_station, 192, LV_SIZE_CONTENT);
            lv_label_set_long_mode(_label_station, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_color(_label_station, lv_color_hex(0xffffffff), LV_PART_MAIN);
            lv_obj_set_style_text_font(_label_station, &lv_font_montserrat_14, LV_PART_MAIN);
            lv_label_set_text(_label_station, STATIONS[0].title);
        }
        {
            _label_status = lv_label_create(parent);
            lv_obj_set_pos(_label_status, 11, 29);
            lv_obj_set_size(_label_status, 169, LV_SIZE_CONTENT);
            lv_label_set_long_mode(_label_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_color(_label_status, lv_color_hex(0xffff3f3f), LV_PART_MAIN);
            lv_obj_set_style_text_font(_label_status, &lv_font_montserrat_14, LV_PART_MAIN);
            lv_label_set_text(_label_status, LV_SYMBOL_WIFI " Ready");
        }
        {
            _label_station_nos = lv_label_create(parent);
            lv_obj_set_pos(_label_station_nos, 82, 55);
            lv_obj_set_size(_label_station_nos, 67, LV_SIZE_CONTENT);
            lv_label_set_long_mode(_label_station_nos, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_color(_label_station_nos, lv_color_hex(0xff13ff00), LV_PART_MAIN);
            lv_obj_set_style_text_font(_label_station_nos, &lv_font_montserrat_14, LV_PART_MAIN);
            lv_obj_set_style_text_align(_label_station_nos, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            lv_label_set_text_fmt(_label_station_nos, "%d/%d", 1, _station_count);
        }
        {
            _arc_volume = lv_arc_create(parent);
            lv_obj_set_pos(_arc_volume, 164, -7);
            lv_obj_set_size(_arc_volume, 150, 150);
            lv_arc_set_value(_arc_volume, _volume * 100 / 21);
            lv_arc_set_bg_start_angle(_arc_volume, 125);
            lv_arc_set_bg_end_angle(_arc_volume, 235);
            lv_obj_set_style_arc_width(_arc_volume, 3, LV_PART_MAIN);
            lv_obj_set_style_arc_color(_arc_volume, lv_color_hex(0xff000000), LV_PART_MAIN);
            lv_obj_set_style_arc_width(_arc_volume, 3, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(_arc_volume, lv_color_hex(0xffcfcd31), LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(_arc_volume, 0, LV_PART_KNOB);
        }
        {
            _img_signal = lv_image_create(parent);
            lv_obj_set_pos(_img_signal, 195, 40);
            lv_obj_set_size(_img_signal, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_image_set_src(_img_signal, &img_wifi_04);
            lv_image_set_scale(_img_signal, 180);
            lv_obj_set_style_image_recolor(_img_signal, lv_color_hex(0xff5197f9), LV_PART_MAIN);
            lv_obj_set_style_image_recolor_opa(_img_signal, 200, LV_PART_MAIN);
        }
        {
            _panel_controls = lv_obj_create(parent);
            lv_obj_set_pos(_panel_controls, 5, 86);
            lv_obj_set_size(_panel_controls, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_remove_flag(_panel_controls, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(_panel_controls, 256, LV_PART_MAIN);
            lv_obj_set_style_border_width(_panel_controls, 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(_panel_controls, lv_color_hex(0xff3f4147), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(_panel_controls, 0, LV_PART_MAIN);
            lv_obj_set_style_layout(_panel_controls, LV_LAYOUT_FLEX, LV_PART_MAIN);
            lv_obj_set_style_flex_flow(_panel_controls, LV_FLEX_FLOW_ROW, LV_PART_MAIN);
            lv_obj_set_style_flex_main_place(_panel_controls, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_flex_cross_place(_panel_controls, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_flex_track_place(_panel_controls, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_pad_all(_panel_controls, 5, LV_PART_MAIN);
            lv_obj_set_style_pad_column(_panel_controls, 5, LV_PART_MAIN);

            {
                lv_obj_t* panel_parent = _panel_controls;

                {
                    _img_prev = lv_image_create(panel_parent);
                    lv_obj_set_size(_img_prev, 30, 30);
                    lv_image_set_src(_img_prev, &img_music_previous);
                    lv_obj_set_style_image_recolor(_img_prev, lv_color_hex(0xff13ff00), LV_PART_MAIN | LV_STATE_FOCUS_KEY);
                    lv_obj_set_style_image_recolor_opa(_img_prev, 255, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
                }
                {
                    _img_play = lv_image_create(panel_parent);
                    lv_obj_set_size(_img_play, 30, 30);
                    lv_image_set_src(_img_play, &img_music_play);
                    lv_obj_set_style_image_recolor(_img_play, lv_color_hex(0xffff5b00), LV_PART_MAIN | LV_STATE_CHECKED);
                    lv_obj_set_style_image_recolor_opa(_img_play, 255, LV_PART_MAIN | LV_STATE_CHECKED);
                    lv_obj_set_style_image_recolor(_img_play, lv_color_hex(0xff13ff00), LV_PART_MAIN | LV_STATE_FOCUS_KEY);
                    lv_obj_set_style_image_recolor_opa(_img_play, 255, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
                }
                {
                    _img_next = lv_image_create(panel_parent);
                    lv_obj_set_size(_img_next, 30, 30);
                    lv_image_set_src(_img_next, &img_music_next);
                    lv_obj_set_style_image_recolor(_img_next, lv_color_hex(0xff13ff00), LV_PART_MAIN | LV_STATE_FOCUS_KEY);
                    lv_obj_set_style_image_recolor_opa(_img_next, 255, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
                }
            }
        }
    }

    updateStationUI();
    updatePlayPauseUI();
    updateVolumeUI();

    _tmr = lv_timer_create(timerCb, 200, this);

    loadScreen(SCREEN_ID_MAIN);
    lv_scr_load(_screen);
}

void WebRadioApp::onUpdate() {
}

void WebRadioApp::onDestroy() {
    stopPlayback();

    if (_tmr) {
        lv_timer_del(_tmr);
        _tmr = nullptr;
    }

    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }

    if (_ring_buf) {
        free(_ring_buf);
        _ring_buf = nullptr;
    }

    M5.Speaker.begin();
}

void WebRadioApp::playStation(int idx) {
    if (idx < 0 || idx >= _station_count) return;

    stopPlayback();

    _selected = idx;
    _connecting = true;
    _stop_requested = false;
    _stream_error = false;
    _ring_head = 0;
    _ring_tail = 0;
    _ring_count = 0;

    s_volume = _volume;
    s_first_frame = true;

    setupI2S(44100, true);
    s_i2s_tx = _i2s_tx;

    if (s_mp3) {
        delete s_mp3;
        s_mp3 = nullptr;
    }
    s_mp3 = new libhelix::MP3DecoderHelix();
    s_mp3->setDataCallback(mp3DataCb);
    s_mp3->begin();

    _net_task_run = true;
    xTaskCreatePinnedToCore(netTaskFunc, "webradio_net", 8192, this, 4, &_net_task, 0);

    _audio_task_run = true;
    xTaskCreatePinnedToCore(audioTaskFunc, "webradio_audio", 16384, this, 5, &_audio_task, 0);
}

void WebRadioApp::stopPlayback() {
    _stop_requested = true;
    _net_task_run = false;
    _audio_task_run = false;

    vTaskDelay(100);

    if (_net_task) {
        vTaskDelete(_net_task);
        _net_task = nullptr;
    }
    if (_audio_task) {
        vTaskDelete(_audio_task);
        _audio_task = nullptr;
    }

    if (s_mp3) {
        s_mp3->end();
        delete s_mp3;
        s_mp3 = nullptr;
    }

    teardownI2S();
    s_i2s_tx = nullptr;

    _playing = false;
    _connecting = false;
    _stop_requested = false;
    _stream_error = false;

    _ring_head = 0;
    _ring_tail = 0;
    _ring_count = 0;
}

void WebRadioApp::goBack() {
    if (_playing || _connecting || _net_task || _audio_task) {
        stopPlayback();
    }
    if (_back_app) {
        _manager.switchApp(_back_app);
    }
}

void WebRadioApp::updateStationUI() {
    if (!_label_station) return;
    lv_label_set_text(_label_station, STATIONS[_selected].title);
    if (_label_station_nos) {
        lv_label_set_text_fmt(_label_station_nos, "%d/%d", _selected + 1, _station_count);
    }
}

void WebRadioApp::updatePlayPauseUI() {
    if (!_img_play) return;
    if (_playing) {
        lv_obj_add_state(_img_play, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(_img_play, LV_STATE_CHECKED);
    }
}

void WebRadioApp::updateVolumeUI() {
    if (!_arc_volume) return;
    lv_arc_set_value(_arc_volume, _volume * 100 / 21);
}

void WebRadioApp::updateStatusUI() {
    if (!_label_status) return;

    if (_stream_error) {
        lv_label_set_text(_label_status, LV_SYMBOL_WARNING " Error");
        lv_obj_set_style_text_color(_label_status, lv_color_hex(0xffff3f3f), LV_PART_MAIN);
    } else if (_connecting) {
        lv_label_set_text(_label_status, LV_SYMBOL_WIFI " Connecting...");
        lv_obj_set_style_text_color(_label_status, lv_color_hex(0xffcfcd31), LV_PART_MAIN);
    } else if (_playing) {
        lv_label_set_text(_label_status, LV_SYMBOL_PLAY " Playing");
        lv_obj_set_style_text_color(_label_status, lv_color_hex(0xff13ff00), LV_PART_MAIN);
    } else {
        lv_label_set_text(_label_status, LV_SYMBOL_STOP " Stopped");
        lv_obj_set_style_text_color(_label_status, lv_color_hex(0xff5197f9), LV_PART_MAIN);
    }
}

void WebRadioApp::onKeyPressed(char key) {
    if (key == '`' || key == 0x1B) {
        if (atoi(ConfigManager::getVolume()) > 0) M5.Speaker.tone(3000, 20);
        goBack();
        return;
    }

    switch (key) {
        case '.':
            if (_station_count > 0) {
                flashControl(_img_next);
                _selected = (_selected + 1) % _station_count;
                updateStationUI();
                if (_playing || _connecting) {
                    playStation(_selected);
                }
                updatePlayPauseUI();
            }
            break;

        case ';':
            if (_station_count > 0) {
                flashControl(_img_prev);
                _selected = (_selected - 1 + _station_count) % _station_count;
                updateStationUI();
                if (_playing || _connecting) {
                    playStation(_selected);
                }
                updatePlayPauseUI();
            }
            break;

        case 'p': case 'P':
            flashControl(_img_play);
            if (_playing || _connecting) {
                stopPlayback();
            } else {
                if (WiFi.status() == WL_CONNECTED) {
                    playStation(_selected);
                } else {
                    lv_label_set_text(_label_status, LV_SYMBOL_WARNING " No WiFi");
                    lv_obj_set_style_text_color(_label_status, lv_color_hex(0xffff3f3f), LV_PART_MAIN);
                }
            }
            updatePlayPauseUI();
            updateStatusUI();
            break;

        case '=': case '+':
            _volume += 2;
            if (_volume > 21) _volume = 21;
            s_volume = _volume;
            updateVolumeUI();
            break;

        case '-': case '_':
            _volume -= 2;
            if (_volume < 0) _volume = 0;
            s_volume = _volume;
            updateVolumeUI();
            break;
    }
}
