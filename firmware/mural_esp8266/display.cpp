#include "display.h"

Display::Display(UnoBridge* bridge) : bridge(bridge) {}

void Display::showStarting() {
    bridge->sendOledLines("Starting up...");
}

void Display::showConnecting() {
    bridge->sendOledLines("Connecting", "to WiFi...");
}

void Display::showHotspot() {
    bridge->sendOledLines("Connect to hotspot", "SSID: Mural");
}

void Display::showHotspotReady(const String& ipAddress) {
    String url = "http://" + ipAddress;
    bridge->sendOledLines("SSID: Mural", url.c_str());
}

void Display::showConnected(const String& ipAddress) {
    bridge->sendOledLines(ipAddress.c_str(), "http://mural.local");
}

void Display::showCalibration(double x, double y) {
    char buf[24];
    snprintf(buf, sizeof(buf), "X:%.1f Y:%.1f", x, y);
    bridge->sendOledLines(buf);
}

void Display::showDrawing(int progress) {
    bridge->sendOledProgress(progress);
}

void Display::showPaused(int progress) {
    char buf[24];
    snprintf(buf, sizeof(buf), "Paused (%d%%)", constrain(progress, 0, 100));
    bridge->sendOledLines(buf, "Use browser");
}

void Display::showFinished() {
    bridge->sendOledLines("Drawing complete!", "Ready");
}
