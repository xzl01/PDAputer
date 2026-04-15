#include "sd_manager.h"
#include <M5Cardputer.h>
#include <driver/gpio.h>
#include <SPI.h>
#include <SD.h>

static bool _mounted = false;
static SPIClass _sdSPI;

bool SDManager::begin() {
    if (_mounted) return true;

    Serial.println("[SD] Starting...");

    int8_t sd_sck = M5.getPin(m5::pin_name_t::sd_spi_sclk);
    int8_t sd_miso = M5.getPin(m5::pin_name_t::sd_spi_miso);
    int8_t sd_mosi = M5.getPin(m5::pin_name_t::sd_spi_mosi);
    int8_t sd_ss = M5.getPin(m5::pin_name_t::sd_spi_ss);

    Serial.printf("[SD] Pins - SCK:%d MISO:%d MOSI:%d SS:%d\n", sd_sck, sd_miso, sd_mosi, sd_ss);

    // Power SD card slot (Cardputer ADV needs GPIO5 high for SD slot power)
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    delay(300);

    // Initialize SPI for SD card
    _sdSPI.begin(sd_sck, sd_miso, sd_mosi, sd_ss);
    SPI.setHwCs(false);
    SPI.setBitOrder(MSBFIRST);
    SPI.setDataMode(SPI_MODE0);
    SPI.setClockDivider(SPI_CLOCK_DIV4);  // ~10MHz

    // Mount SD card using Arduino SD library
    // Default mountpoint is "/sd", registered to VFS at that path
    _mounted = SD.begin(sd_ss, _sdSPI);

    if (_mounted) {
        Serial.println("[SD] Card mounted!");
        uint32_t size = SD.totalBytes();
        Serial.printf("[SD] Card size: %u MB\n", size / (1024 * 1024));
        Serial.println("[SD] VFS path '/sd' registered by Arduino SD library");
        Serial.println("[SD] TinyTF should be able to open /sd/<path> via LVGL's lv_fs_f_open()");
    } else {
        Serial.println("[SD] Card mount failed");
    }

    return _mounted;
}

bool SDManager::isMounted() {
    return _mounted;
}
