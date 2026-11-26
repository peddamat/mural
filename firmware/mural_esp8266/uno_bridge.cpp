#include "uno_bridge.h"
#include "config.h"
#include <stdlib.h>

namespace {
constexpr unsigned long UNO_TIMEOUT_MS = 3000;
constexpr unsigned long ACK_TIMEOUT_MS = 250;
constexpr unsigned long BUSY_HOLD_MS   = 80;
constexpr uint8_t MIN_FREE_SLOTS_FOR_ENQUEUE = 2;
} // namespace

UnoBridge::UnoBridge()
    : rxLen(0),
      freeSlots(8),
      busy(false),
      posL(0),
      posR(0),
      lastContactMs(0),
      lastPingMs(0),
      awaitingAck(false),
      ackSentMs(0),
      busyHoldUntilMs(0) {}

void UnoBridge::begin() {
    Serial.begin(LINK_BAUD_RATE);
    delay(50);
    // Send a clean newline first to flush any ESP8266 boot ROM chatter on Uno's RX
    Serial.print('\n');
    sendPing();
}

void UnoBridge::update() {
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            rxBuf[rxLen] = '\0';
            if (rxLen > 0) {
                parseLine(rxBuf);
            }
            rxLen = 0;
        } else if (rxLen < sizeof(rxBuf) - 1) {
            rxBuf[rxLen++] = c;
        } else {
            rxLen = 0;
        }
    }

    unsigned long now = millis();
    if (awaitingAck && (now - ackSentMs >= ACK_TIMEOUT_MS)) {
        awaitingAck = false;
        sendPing();
    }

    if (!isConnected() && (now - lastPingMs >= 1000)) {
        sendPing();
    }
}

void UnoBridge::parseLine(char* line) {
    // Only parse framed replies starting with "@Q "
    if (line[0] != '@' || line[1] != 'Q') {
        return;
    }

    char* p = line + 2;
    long f = strtol(p, &p, 10);
    long b = strtol(p, &p, 10);
    long l = strtol(p, &p, 10);
    long r = strtol(p, &p, 10);

    freeSlots = (f < 0) ? 0 : static_cast<uint8_t>(f);
    posL = static_cast<int32_t>(l);
    posR = static_cast<int32_t>(r);
    lastContactMs = millis();
    awaitingAck = false;

    if (b != 0) {
        busy = true;
    } else if (millis() >= busyHoldUntilMs) {
        busy = false;
    }
}

bool UnoBridge::isConnected() const {
    return lastContactMs != 0 && (millis() - lastContactMs < UNO_TIMEOUT_MS);
}

bool UnoBridge::isBusy() const {
    return busy || (millis() < busyHoldUntilMs);
}

bool UnoBridge::canEnqueue() const {
    return !awaitingAck && freeSlots >= MIN_FREE_SLOTS_FOR_ENQUEUE;
}

void UnoBridge::markMotionSent() {
    busy = true;
    busyHoldUntilMs = millis() + BUSY_HOLD_MS;
    awaitingAck = true;
    ackSentMs = millis();
}

void UnoBridge::sendPing() {
    Serial.println(F("$?"));
    lastPingMs = millis();
}

void UnoBridge::setPosition(int32_t leftSteps, int32_t rightSteps) {
    char buf[40];
    snprintf(buf, sizeof(buf), "$P %ld %ld", static_cast<long>(leftSteps), static_cast<long>(rightSteps));
    Serial.println(buf);
    posL = leftSteps;
    posR = rightSteps;
    busy = false;
    busyHoldUntilMs = 0;
    awaitingAck = true;
    ackSentMs = millis();
}

bool UnoBridge::queueMoveTo(int32_t leftSteps, int32_t rightSteps, float leftSpeed, float rightSpeed, bool force) {
    if (!force && !canEnqueue()) {
        return false;
    }
    char buf[64];
    snprintf(
        buf, sizeof(buf),
        "$M %ld %ld %.1f %.1f",
        static_cast<long>(leftSteps),
        static_cast<long>(rightSteps),
        static_cast<double>(leftSpeed),
        static_cast<double>(rightSpeed)
    );
    Serial.println(buf);
    if (freeSlots > 0) {
        freeSlots--;
    }
    markMotionSent();
    return true;
}

void UnoBridge::extendRelative(int32_t steps, float speed) {
    char buf[40];
    snprintf(buf, sizeof(buf), "$R %ld %.1f", static_cast<long>(steps), static_cast<double>(speed));
    Serial.println(buf);
    markMotionSent();
}

void UnoBridge::jogLeft(int dir, float speed) {
    char buf[32];
    snprintf(buf, sizeof(buf), "$L %d %.1f", dir, static_cast<double>(speed));
    Serial.println(buf);
    if (dir != 0) {
        markMotionSent();
    } else {
        awaitingAck = true;
        ackSentMs = millis();
    }
}

void UnoBridge::jogRight(int dir, float speed) {
    char buf[32];
    snprintf(buf, sizeof(buf), "$K %d %.1f", dir, static_cast<double>(speed));
    Serial.println(buf);
    if (dir != 0) {
        markMotionSent();
    } else {
        awaitingAck = true;
        ackSentMs = millis();
    }
}

void UnoBridge::stopAll() {
    Serial.println(F("$X"));
    busy = false;
    busyHoldUntilMs = 0;
    awaitingAck = true;
    ackSentMs = millis();
}

void UnoBridge::setServoImmediate(int angle) {
    char buf[16];
    snprintf(buf, sizeof(buf), "$S %d", constrain(angle, 0, 180));
    Serial.println(buf);
}

bool UnoBridge::queueSlowServo(int targetAngle, int degPerSec, bool force) {
    if (!force && !canEnqueue()) {
        return false;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "$V %d %d", constrain(targetAngle, 0, 180), max(1, degPerSec));
    Serial.println(buf);
    if (freeSlots > 0) {
        freeSlots--;
    }
    markMotionSent();
    return true;
}

void UnoBridge::sendOledLines(const char* line1, const char* line2) {
    Serial.print(F("$O "));
    if (line1) {
        Serial.print(line1);
    }
    if (line2 && line2[0] != '\0') {
        Serial.print('|');
        Serial.print(line2);
    }
    Serial.println();
}

void UnoBridge::sendOledProgress(int percent) {
    char buf[16];
    snprintf(buf, sizeof(buf), "$B %d", constrain(percent, 0, 100));
    Serial.println(buf);
}
