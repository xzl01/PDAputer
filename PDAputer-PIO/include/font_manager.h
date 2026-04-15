#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include <lvgl.h>

namespace FontManager {
    // Initialize TinyTF CJK font from SD card
    // Call after SDManager::begin() is successful
    bool begin();

    // Get the TinyTF CJK fallback font
    lv_font_t* getCjkFont();

    // Check if CJK font was loaded successfully
    bool isCjkFontLoaded();
}

#endif // FONT_MANAGER_H
