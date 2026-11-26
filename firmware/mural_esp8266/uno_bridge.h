#ifndef MURAL_ESP8266_UNO_BRIDGE_H
#define MURAL_ESP8266_UNO_BRIDGE_H

#include <Arduino.h>

class UnoBridge {
public:
    UnoBridge();

    void begin();
    void update();

    // Connection and live motion state reported by ATmega328P
    bool isConnected() const;
    bool isBusy() const;
    uint8_t getFreeQueueSlots() const { return freeSlots; }
    bool canEnqueue() const;
    int32_t getLeftPos() const { return posL; }
    int32_t getRightPos() const { return posR; }

    // Motion & hardware commands sent to ATmega328P
    void sendPing();
    void setPosition(int32_t leftSteps, int32_t rightSteps);
    bool queueMoveTo(int32_t leftSteps, int32_t rightSteps, float leftSpeed, float rightSpeed, bool force = false);
    void extendRelative(int32_t steps, float speed);
    void jogLeft(int dir, float speed);
    void jogRight(int dir, float speed);
    void stopAll();

    void setServoImmediate(int angle);
    bool queueSlowServo(int targetAngle, int degPerSec, bool force = false);

    void sendOledLines(const char* line1, const char* line2 = nullptr);
    void sendOledProgress(int percent);

private:
    char rxBuf[80];
    uint8_t rxLen;

    uint8_t freeSlots;
    bool busy;
    int32_t posL;
    int32_t posR;

    unsigned long lastContactMs;
    unsigned long lastPingMs;
    bool awaitingAck;
    unsigned long ackSentMs;
    unsigned long busyHoldUntilMs;

    void parseLine(char* line);
    void markMotionSent();
};

#endif // MURAL_ESP8266_UNO_BRIDGE_H
