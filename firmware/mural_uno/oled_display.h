#ifndef MURAL_UNO_OLED_DISPLAY_H
#define MURAL_UNO_OLED_DISPLAY_H

#include <Arduino.h>

// Lightweight, zero-framebuffer SSD1306 128x32 I2C display driver.
// Uses 0 bytes of SRAM framebuffer (5x7 font stored in PROGMEM) so ATmega328P
// keeps plenty of SRAM headroom, and invokes an optional stepper-service
// callback between I2C chunks so stepper motion never stutters during refreshes.
class OledDisplay {
public:
    typedef void (*YieldCallback)();

    OledDisplay();

    // Probes I2C at OLED_I2C_ADDR (0x3C). Returns true if OLED is present.
    // If not connected, all draw methods safely become no-ops.
    bool begin(YieldCallback yieldCb = nullptr);
    bool isPresent() const { return present; }

    // Clears screen and draws 1 or 2 centered text lines.
    void showLines(const char* line1, const char* line2 = nullptr);

    // Draws a bordered progress bar with percentage text (0-100).
    void showProgress(int percent);

private:
    bool present;
    YieldCallback onYield;

    void serviceYield();
    void sendCommand(uint8_t cmd);
    void sendCommands(const uint8_t* cmds, uint8_t len);
    void setPageColumn(uint8_t page, uint8_t col);
    void clearPage(uint8_t page);
    void drawCenteredTextPage(uint8_t page, const char* text);
};

#endif // MURAL_UNO_OLED_DISPLAY_H
