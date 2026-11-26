#ifndef MURAL_ESP8266_PHASE_MANAGER_H
#define MURAL_ESP8266_PHASE_MANAGER_H

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include "movement.h"
#include "pen.h"
#include "stream_runner.h"
#include "display.h"
#include "uno_bridge.h"

class PhaseManager {
public:
    enum Phase : uint8_t {
        SetTopDistance = 0,
        SvgSelect      = 1,
        RetractBelts   = 2,
        ExtendToHome   = 3,
        PenCalibration = 4,
        BeginDrawing   = 5,
        Drawing        = 6
    };

    PhaseManager(
        ESP8266WebServer* server,
        UnoBridge* bridge,
        Movement* movement,
        Pen* pen,
        StreamRunner* runner,
        Display* display
    );

    void registerRoutes();
    void loop();
    void reset();

private:
    ESP8266WebServer* server;
    UnoBridge* bridge;
    Movement* movement;
    Pen* pen;
    StreamRunner* runner;
    Display* display;
    Phase currentPhase;

    const char* getPhaseName() const;
    void addCorsHeaders();
    void sendStateJson();
    void sendStreamStatusJson(int accepted = -1);
    void sendNotSupported();

    // Route handlers
    void handleGetState();
    void handleCommand();
    void handleSetTopDistance();
    void handleSetPulleyDiameter();
    void handleExtendToHome();
    void handleSetServo();
    void handleSetPenDistance();
    void handleEstepsCalibration();
    void handleDoneWithPhase();
    void handleResumeTopDistance();
    void handleRun();
    void handleStreamBatch();
    void handleStreamStatus();
    void handlePause();
    void handleResumeStream();
    void handleStop();
};

#endif // MURAL_ESP8266_PHASE_MANAGER_H
