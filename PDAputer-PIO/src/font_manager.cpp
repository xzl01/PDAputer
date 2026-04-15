#include "font_manager.h"
#include "sd_manager.h"
#include <lvgl.h>

// CJK font loaded from SD card via TinyTF
static lv_font_t* _cjk_font = nullptr;

bool FontManager::begin() {
    if (!SDManager::isMounted()) {
        Serial.println("[FONT] SD card not mounted, skipping CJK font");
        return false;
    }

    Serial.println("[FONT] Loading CJK font from SD...");

    // Try loading from common font paths
    const char* paths[] = {
        "/sd/fonts/NotoSansCJK_subset.ttf",
        "/sd/NotoSansCJK_subset.ttf",
        "/sd/fonts/NotoSansCJK-SC.ttf",
        "/sd/NotoSansCJK-SC.ttf",
    };

    for (int i = 0; i < 4; i++) {
        _cjk_font = lv_tiny_ttf_create_file(paths[i], 16);
        if (_cjk_font != nullptr) {
            Serial.printf("[FONT] CJK font loaded from: %s\n", paths[i]);
            break;
        }
    }

    if (_cjk_font == nullptr) {
        Serial.println("[FONT] Failed to load CJK TinyTF font");
        return false;
    }

    // Set as fallback font for the global LV_FONT_DEFAULT
    // This provides CJK fallback for ANY widget that uses LV_FONT_DEFAULT
    // (widgets with explicit font set still need their fallback configured)
    lv_font_t* default_font = (lv_font_t*)LV_FONT_DEFAULT;
    if (default_font && default_font->fallback == nullptr) {
        default_font->fallback = _cjk_font;
        Serial.println("[FONT] CJK TinyTF set as fallback for LV_FONT_DEFAULT");
    } else if (default_font) {
        Serial.println("[FONT] LV_FONT_DEFAULT already has fallback, skipping");
    }

    Serial.println("[FONT] Font initialization complete!");
    return true;
}

lv_font_t* FontManager::getCjkFont() {
    return _cjk_font;
}

bool FontManager::isCjkFontLoaded() {
    return _cjk_font != nullptr;
}
