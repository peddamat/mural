// ============================================================================
// Mural v2 - ESP8266 WiFi, Web Server, Kinematics & Browser Streaming Firmware
// Target: HW-649 (UNO+WiFi R3 ATmega328P+ESP8266 32Mb/4MB Flash)
//
// Responsibilities:
//   - Connects to WiFi (or hosts "Mural" hotspot via WiFiManager) + mDNS (mural.local)
//   - Serves the Mural React Web UI from LittleFS (/www/)
//   - Runs the full 64-bit Mural v2 kinematic solver & 1mm linear interpolator
//   - Streams drawing commands live from the web browser (NO SD card required!)
//   - Sends low-level step/servo/OLED commands over UART to the ATmega328P
//
// HW-649 DIP Switch Settings:
//   - Flash this sketch + LittleFS (ESP8266): SW 5,6,7 ON (all others OFF)
//   - Normal operation (ESP8266 <-> ATmega328P): SW 1,2 ON (all others OFF)
// ============================================================================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <WiFiManager.h>

#include "config.h"
#include "uno_bridge.h"
#include "display.h"
#include "movement.h"
#include "pen.h"
#include "stream_runner.h"
#include "phase_manager.h"
#include "portalhtml.h"

namespace {

constexpr const char* HOTSPOT_SSID = "Mural";

// Stored in ESP8266 RTC user memory so "Use without WiFi" survives ESP.restart()
// but clears on a cold power cycle.
constexpr uint32_t HOTSPOT_BOOT_MAGIC = 0x4D555241; // "MURA"

ESP8266WebServer server(80);
UnoBridge unoBridge;
Display* display = nullptr;
Movement* movement = nullptr;
Pen* pen = nullptr;
StreamRunner* runner = nullptr;
PhaseManager* phaseManager = nullptr;

String getContentType(const String& path) {
    if (path.endsWith(".html")) return F("text/html");
    if (path.endsWith(".css"))  return F("text/css");
    if (path.endsWith(".js"))   return F("application/javascript");
    if (path.endsWith(".wasm")) return F("application/wasm");
    if (path.endsWith(".svg"))  return F("image/svg+xml");
    if (path.endsWith(".png"))  return F("image/png");
    if (path.endsWith(".ico"))  return F("image/x-icon");
    if (path.endsWith(".json")) return F("application/json");
    return F("text/plain");
}

bool serveStaticAsset(String uriPath) {
    if (uriPath.endsWith("/")) {
        uriPath += "index.html";
    }
    if (uriPath.indexOf("..") >= 0) {
        return false;
    }

    String contentType = getContentType(uriPath);
    String fsPath = "/www" + uriPath;
    String gzPath = fsPath + ".gz";

    if (LittleFS.exists(gzPath)) {
        File file = LittleFS.open(gzPath, "r");
        if (file) {
            server.sendHeader(F("Cache-Control"), F("no-cache"));
            // ESP8266WebServer::streamFile automatically sets Content-Encoding: gzip for .gz files
            server.streamFile(file, contentType);
            file.close();
            return true;
        }
    }

    if (LittleFS.exists(fsPath)) {
        File file = LittleFS.open(fsPath, "r");
        if (file) {
            server.sendHeader(F("Cache-Control"), F("no-cache"));
            server.streamFile(file, contentType);
            file.close();
            return true;
        }
    }

    return false;
}

void handleNotFound() {
    if (server.method() == HTTP_GET && serveStaticAsset(server.uri())) {
        return;
    }

    if (server.uri() == "/") {
        // Helpful fallback page if LittleFS web UI hasn't been uploaded yet
        String ip = (WiFi.getMode() == WIFI_AP)
            ? WiFi.softAPIP().toString()
            : WiFi.localIP().toString();
        String html = F(
            "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Mural HW-649</title></head>"
            "<body style='margin:0;padding:2em;background:#030712;color:#f3f4f6;font-family:system-ui,sans-serif'>"
            "<h2>Mural v2 (HW-649) Firmware Running</h2>"
            "<p>The web UI files are not uploaded to LittleFS yet.</p>"
            "<p>Either upload LittleFS (<code>pio run -e hw649_esp8266 -t uploadfs</code>) or run the UI locally "
            "and open it with <code>?host=http://"
        );
        html += ip;
        html += F("</code>.</p></body></html>");
        server.send(200, F("text/html"), html);
        return;
    }

    server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    server.send(404, F("text/plain"), F("Not found"));
}

bool consumeHotspotBootFlag() {
    uint32_t flag = 0;
    ESP.rtcUserMemoryRead(0, &flag, sizeof(flag));
    if (flag == HOTSPOT_BOOT_MAGIC) {
        uint32_t zero = 0;
        ESP.rtcUserMemoryWrite(0, &zero, sizeof(zero));
        return true;
    }
    return false;
}

void setHotspotBootFlag() {
    uint32_t magic = HOTSPOT_BOOT_MAGIC;
    ESP.rtcUserMemoryWrite(0, &magic, sizeof(magic));
}

} // namespace

void setup() {
    delay(50);

    // Serial connects directly to ATmega328P when HW-649 DIP switches 1 & 2 are ON.
    // Keep Serial dedicated exclusively to the framed UnoBridge protocol.
    unoBridge.begin();

    LittleFS.begin();

    display = new Display(&unoBridge);
    display->showStarting();

    movement = new Movement(&unoBridge, display);
    pen = new Pen(&unoBridge);
    runner = new StreamRunner(movement, pen, display);

    bool hotspotMode = consumeHotspotBootFlag();
    String ipAddress;

    if (hotspotMode) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(HOTSPOT_SSID);
        ipAddress = WiFi.softAPIP().toString();
    } else {
        WiFiManager wifiManager;
        wifiManager.setDebugOutput(false); // Never pollute ATmega328P UART link

        bool resetAfterConnect = false;
        wifiManager.setConnectTimeout(20);
        applyPortalTheme(wifiManager);

        wifiManager.setWebServerCallback([&]() {
            resetAfterConnect = true;
            wifiManager.server->on("/offline", HTTP_POST, [&]() {
                setHotspotBootFlag();
                wifiManager.server->send(200, "text/html", OFFLINE_PAGE_HTML);
                wifiManager.stopConfigPortal();
            });
        });

        wifiManager.setAPCallback([](WiFiManager*) {
            display->showHotspot();
        });

        display->showConnecting();
        wifiManager.autoConnect(HOTSPOT_SSID);

        if (resetAfterConnect) {
            delay(200);
            ESP.restart();
        }

        ipAddress = WiFi.localIP().toString();
    }

    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    MDNS.begin("mural");

    phaseManager = new PhaseManager(&server, &unoBridge, movement, pen, runner, display);
    phaseManager->registerRoutes();
    server.onNotFound(handleNotFound);
    server.begin();

    if (hotspotMode) {
        display->showHotspotReady(ipAddress);
    } else {
        display->showConnected(ipAddress);
    }
}

void loop() {
    server.handleClient();
    MDNS.update();
    unoBridge.update();
    runner->run();
    phaseManager->loop();
}
