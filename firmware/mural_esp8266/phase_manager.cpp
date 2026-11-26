#include "phase_manager.h"
#include "config.h"
#include <stdlib.h>

namespace {
constexpr double PEN_CAL_STEP_MM = 100.0;
} // namespace

PhaseManager::PhaseManager(
    ESP8266WebServer* server,
    UnoBridge* bridge,
    Movement* movement,
    Pen* pen,
    StreamRunner* runner,
    Display* display
)
    : server(server),
      bridge(bridge),
      movement(movement),
      pen(pen),
      runner(runner),
      display(display),
      currentPhase(SetTopDistance) {}

void PhaseManager::addCorsHeaders() {
    server->sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    server->sendHeader(F("Access-Control-Allow-Methods"), F("GET, POST, OPTIONS"));
    server->sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type"));
}

const char* PhaseManager::getPhaseName() const {
    switch (currentPhase) {
        case SetTopDistance: return "SetTopDistance";
        case SvgSelect:      return "SvgSelect";
        case RetractBelts:   return "RetractBelts";
        case ExtendToHome:   return "ExtendToHome";
        case PenCalibration: return "PenCalibration";
        case BeginDrawing:   return "BeginDrawing";
        case Drawing:        return "Drawing";
        default:             return "SetTopDistance";
    }
}

void PhaseManager::reset() {
    runner->reset();
    movement->resetSession();
    currentPhase = SetTopDistance;
}

void PhaseManager::sendNotSupported() {
    addCorsHeaders();
    server->send(400, F("text/plain"), F("Request is not supported by the current server phase"));
}

void PhaseManager::sendStateJson() {
    Movement::Point home = movement->getHomeCoordinates();
    int topDistance = movement->getTopDistance();
    double safeWidth = (topDistance != -1) ? movement->getWidth() : -1.0;

    char json[384];
    snprintf(
        json, sizeof(json),
        "{\"phase\":\"%s\",\"moving\":%s,\"startedHoming\":%s,\"topDistance\":%d,"
        "\"pulleyDiameter\":%.6f,\"safeWidth\":%.2f,\"safeXFraction\":%.2f,"
        "\"safeYFraction\":%.2f,\"homeX\":%.2f,\"homeY\":%.2f,"
        "\"streamingMode\":true,\"unoConnected\":%s}",
        getPhaseName(),
        movement->isMoving() ? "true" : "false",
        movement->hasStartedHoming() ? "true" : "false",
        topDistance,
        movement->getPulleyDiameter(),
        safeWidth,
        SAFE_X_FRACTION,
        SAFE_Y_FRACTION,
        home.x,
        home.y,
        bridge->isConnected() ? "true" : "false"
    );

    addCorsHeaders();
    server->send(200, F("application/json"), json);
}

void PhaseManager::sendStreamStatusJson(int accepted) {
    char json[320];
    snprintf(
        json, sizeof(json),
        "{\"state\":\"%s\",\"accepted\":%d,\"nextSeq\":%lu,\"executed\":%lu,"
        "\"totalCommands\":%lu,\"buffered\":%u,\"capacity\":%u,"
        "\"progress\":%d,\"streamDone\":%s,\"unoConnected\":%s,\"unoFree\":%u}",
        runner->getStateName(),
        accepted,
        static_cast<unsigned long>(runner->getNextExpectedSeq()),
        static_cast<unsigned long>(runner->getExecutedCount()),
        static_cast<unsigned long>(runner->getTotalCommands()),
        static_cast<unsigned int>(runner->getBufferedCount()),
        static_cast<unsigned int>(runner->getBufferCapacity()),
        runner->getProgress(),
        runner->isStreamDone() ? "true" : "false",
        bridge->isConnected() ? "true" : "false",
        static_cast<unsigned int>(bridge->getFreeQueueSlots())
    );

    addCorsHeaders();
    server->send(200, F("application/json"), json);
}

void PhaseManager::registerRoutes() {
    auto handleOptions = [this]() {
        addCorsHeaders();
        server->send(204, F("text/plain"), F(""));
    };

    server->on("/getState", HTTP_GET, [this]() { handleGetState(); });
    server->on("/getState", HTTP_OPTIONS, handleOptions);

    server->on("/command", HTTP_POST, [this]() { handleCommand(); });
    server->on("/command", HTTP_OPTIONS, handleOptions);

    server->on("/setTopDistance", HTTP_POST, [this]() { handleSetTopDistance(); });
    server->on("/setTopDistance", HTTP_OPTIONS, handleOptions);

    server->on("/setPulleyDiameter", HTTP_POST, [this]() { handleSetPulleyDiameter(); });
    server->on("/setPulleyDiameter", HTTP_OPTIONS, handleOptions);

    server->on("/extendToHome", HTTP_POST, [this]() { handleExtendToHome(); });
    server->on("/extendToHome", HTTP_OPTIONS, handleOptions);

    server->on("/setServo", HTTP_POST, [this]() { handleSetServo(); });
    server->on("/setServo", HTTP_OPTIONS, handleOptions);

    server->on("/setPenDistance", HTTP_POST, [this]() { handleSetPenDistance(); });
    server->on("/setPenDistance", HTTP_OPTIONS, handleOptions);

    server->on("/estepsCalibration", HTTP_POST, [this]() { handleEstepsCalibration(); });
    server->on("/estepsCalibration", HTTP_OPTIONS, handleOptions);

    server->on("/doneWithPhase", HTTP_POST, [this]() { handleDoneWithPhase(); });
    server->on("/doneWithPhase", HTTP_OPTIONS, handleOptions);

    server->on("/resume", HTTP_POST, [this]() { handleResumeTopDistance(); });
    server->on("/resume", HTTP_OPTIONS, handleOptions);

    server->on("/run", HTTP_POST, [this]() { handleRun(); });
    server->on("/run", HTTP_OPTIONS, handleOptions);

    // Browser streaming endpoints (replaces SD card upload/download)
    server->on("/stream", HTTP_POST, [this]() { handleStreamBatch(); });
    server->on("/stream", HTTP_OPTIONS, handleOptions);

    server->on("/streamStatus", HTTP_GET, [this]() { handleStreamStatus(); });
    server->on("/streamStatus", HTTP_OPTIONS, handleOptions);

    server->on("/pause", HTTP_POST, [this]() { handlePause(); });
    server->on("/pause", HTTP_OPTIONS, handleOptions);

    server->on("/resumeStream", HTTP_POST, [this]() { handleResumeStream(); });
    server->on("/resumeStream", HTTP_OPTIONS, handleOptions);

    server->on("/stop", HTTP_POST, [this]() { handleStop(); });
    server->on("/stop", HTTP_OPTIONS, handleOptions);
}

void PhaseManager::handleGetState() {
    sendStateJson();
}

void PhaseManager::handleCommand() {
    String command = server->arg("command");

    if (currentPhase == SetTopDistance || currentPhase == RetractBelts) {
        if (command == "l-ret")      movement->leftStepper(-1);
        else if (command == "l-ext") movement->leftStepper(1);
        else if (command == "l-0")   movement->leftStepper(0);
        else if (command == "r-ret") movement->rightStepper(-1);
        else if (command == "r-ext") movement->rightStepper(1);
        else if (command == "r-0")   movement->rightStepper(0);
        else {
            addCorsHeaders();
            server->send(400, F("text/plain"), F("Unsupported command"));
            return;
        }
        addCorsHeaders();
        server->send(200, F("text/plain"), F("OK"));
        return;
    }

    if (currentPhase == PenCalibration) {
        if (command != "raise" && command != "lower") {
            addCorsHeaders();
            server->send(400, F("text/plain"), F("Unsupported command"));
            return;
        }
        if (movement->isMoving() || !movement->hasValidCoordinates()) {
            addCorsHeaders();
            server->send(200, F("text/plain"), F("0"));
            return;
        }

        Movement::Point current = movement->getCoordinates();
        double delta = (command == "raise") ? -PEN_CAL_STEP_MM : PEN_CAL_STEP_MM;
        double newY = current.y + delta;
        if (newY < 0.0) {
            addCorsHeaders();
            server->send(200, F("text/plain"), F("0"));
            return;
        }

        float moveTime = 0.0f;
        if (!movement->beginLinearTravel(current.x, newY, PRINT_SPEED_STEPS, &moveTime, true)) {
            addCorsHeaders();
            server->send(500, F("text/plain"), F("Movement error"));
            return;
        }
        display->showCalibration(current.x, newY);
        addCorsHeaders();
        server->send(200, F("text/plain"), String(static_cast<int>(ceil(moveTime)) + 1));
        return;
    }

    sendNotSupported();
}

void PhaseManager::handleSetTopDistance() {
    if (currentPhase != SetTopDistance) {
        sendNotSupported();
        return;
    }
    int distance = server->hasArg("distance") ? server->arg("distance").toInt() : server->arg(0).toInt();
    if (distance <= 0) {
        addCorsHeaders();
        server->send(400, F("text/plain"), F("Invalid distance"));
        return;
    }
    movement->setTopDistance(distance);
    currentPhase = SvgSelect;
    sendStateJson();
}

void PhaseManager::handleSetPulleyDiameter() {
    if (currentPhase != SetTopDistance) {
        sendNotSupported();
        return;
    }
    String raw = server->hasArg("diameter") ? server->arg("diameter") : server->arg(0);
    double diameter = strtod(raw.c_str(), nullptr);
    if (diameter <= 1.0 || diameter >= 100.0) {
        addCorsHeaders();
        server->send(400, F("text/plain"), F("Invalid diameter"));
        return;
    }
    movement->setPulleyDiameter(diameter);
    addCorsHeaders();
    server->send(200, F("text/plain"), F("OK"));
}

void PhaseManager::handleExtendToHome() {
    if (currentPhase != ExtendToHome) {
        sendNotSupported();
        return;
    }
    int moveTime = movement->extendToHome() + 1;
    addCorsHeaders();
    server->send(200, F("text/plain"), String(moveTime));
}

void PhaseManager::handleSetServo() {
    if (currentPhase != SetTopDistance && currentPhase != PenCalibration) {
        sendNotSupported();
        return;
    }
    int angle = server->hasArg("angle") ? server->arg("angle").toInt() : server->arg(0).toInt();
    pen->setRawValue(angle);
    addCorsHeaders();
    server->send(200, F("text/plain"), F("OK"));
}

void PhaseManager::handleSetPenDistance() {
    if (currentPhase != PenCalibration) {
        sendNotSupported();
        return;
    }
    int angle = server->hasArg("angle") ? server->arg("angle").toInt() : server->arg(0).toInt();
    pen->setPenDistance(angle);
    pen->queueSlowUp(true);
    currentPhase = BeginDrawing;
    sendStateJson();
}

void PhaseManager::handleEstepsCalibration() {
    if (currentPhase != SetTopDistance) {
        sendNotSupported();
        return;
    }
    int distanceMm = server->hasArg("distance") ? server->arg("distance").toInt() : server->arg(0).toInt();
    if (distanceMm <= 0) {
        addCorsHeaders();
        server->send(400, F("text/plain"), F("Invalid distance"));
        return;
    }
    movement->extendBelts(distanceMm);
    addCorsHeaders();
    server->send(200, F("text/plain"), F("OK"));
}

void PhaseManager::handleDoneWithPhase() {
    if (currentPhase == SvgSelect) {
        // Browser-streaming mode: commands remain in browser memory/IndexedDB.
        // Advance straight to RetractBelts without uploading to an SD card.
        currentPhase = RetractBelts;
        sendStateJson();
        return;
    }
    if (currentPhase == RetractBelts) {
        movement->leftStepper(0);
        movement->rightStepper(0);
        currentPhase = ExtendToHome;
        sendStateJson();
        return;
    }
    if (currentPhase == BeginDrawing || currentPhase == Drawing) {
        reset();
        sendStateJson();
        return;
    }
    sendNotSupported();
}

void PhaseManager::handleResumeTopDistance() {
    int distance = server->hasArg("distance") ? server->arg("distance").toInt() : server->arg(0).toInt();
    if (distance <= 0) {
        addCorsHeaders();
        server->send(400, F("text/plain"), F("Invalid distance"));
        return;
    }
    movement->resumeTopDistance(distance);
    currentPhase = SvgSelect;
    sendStateJson();
}

void PhaseManager::handleRun() {
    if (currentPhase != BeginDrawing) {
        sendNotSupported();
        return;
    }

    int speed = PRINT_SPEED_STEPS;
    if (server->hasArg("speed")) {
        speed = constrain(server->arg("speed").toInt(), MIN_DRAW_SPEED, MAX_DRAW_SPEED);
    }

    double totalDistance = 0.0;
    if (server->hasArg("totalDistance")) {
        totalDistance = strtod(server->arg("totalDistance").c_str(), nullptr);
    }

    uint32_t totalCommands = 0;
    if (server->hasArg("totalCommands")) {
        totalCommands = strtoul(server->arg("totalCommands").c_str(), nullptr, 10);
    }

    if (!runner->start(speed, totalDistance, totalCommands)) {
        addCorsHeaders();
        server->send(400, F("text/plain"), F("Mural is not homed and ready to draw"));
        return;
    }

    currentPhase = Drawing;
    sendStreamStatusJson(0);
}

void PhaseManager::handleStreamBatch() {
    if (currentPhase != Drawing) {
        sendNotSupported();
        return;
    }

    uint32_t seq = server->hasArg("seq")
        ? strtoul(server->arg("seq").c_str(), nullptr, 10)
        : 0;
    String cmds = server->arg("cmds");
    bool done = server->hasArg("done") && (server->arg("done") == "1" || server->arg("done") == "true");

    int accepted = runner->ingestBatch(seq, cmds, done);
    if (accepted < 0) {
        // Sequence mismatch — return 409 with current nextSeq so browser resyncs immediately
        addCorsHeaders();
        char json[160];
        snprintf(
            json, sizeof(json),
            "{\"error\":\"seq_mismatch\",\"nextSeq\":%lu,\"state\":\"%s\"}",
            static_cast<unsigned long>(runner->getNextExpectedSeq()),
            runner->getStateName()
        );
        server->send(409, F("application/json"), json);
        return;
    }

    sendStreamStatusJson(accepted);
}

void PhaseManager::handleStreamStatus() {
    sendStreamStatusJson(0);
}

void PhaseManager::handlePause() {
    if (currentPhase != Drawing) {
        sendNotSupported();
        return;
    }
    runner->pause();
    sendStreamStatusJson(0);
}

void PhaseManager::handleResumeStream() {
    if (currentPhase != Drawing) {
        sendNotSupported();
        return;
    }
    runner->resume();
    sendStreamStatusJson(0);
}

void PhaseManager::handleStop() {
    if (currentPhase != Drawing) {
        sendNotSupported();
        return;
    }
    runner->stop();
    sendStreamStatusJson(0);
}

void PhaseManager::loop() {
    if (currentPhase == ExtendToHome) {
        if (movement->hasStartedHoming() && !movement->isMoving()) {
            currentPhase = PenCalibration;
        }
    }
}
