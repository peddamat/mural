#ifndef MURAL_ESP8266_DISPLAY_H
#define MURAL_ESP8266_DISPLAY_H

#include <Arduino.h>
#include "uno_bridge.h"

class Display {
public:
    explicit Display(UnoBridge* bridge);

    void showStarting();
    void showConnecting();
    void showHotspot();
    void showHotspotReady(const String& ipAddress);
    void showConnected(const String& ipAddress);
    void showCalibration(double x, double y);
    void showDrawing(int progress);
    void showPaused(int progress);
    void showFinished();

private:
    UnoBridge* bridge;
};

#endif // MURAL_ESP8266_DISPLAY_H
