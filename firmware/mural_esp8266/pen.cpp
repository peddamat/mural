#include "pen.h"
#include "config.h"

Pen::Pen(UnoBridge* bridge)
    : bridge(bridge),
      penDistance(-1),
      currentPosition(PEN_INIT_ANGLE) {
    bridge->setServoImmediate(PEN_INIT_ANGLE);
}

void Pen::setRawValue(int rawValue) {
    int clamped = constrain(rawValue, 0, 180);
    bridge->setServoImmediate(clamped);
    currentPosition = clamped;
}

void Pen::setPenDistance(int value) {
    penDistance = constrain(value, 0, 180);
}

bool Pen::queueSlowUp(bool force) {
    if (!bridge->queueSlowServo(PEN_UP_ANGLE, PEN_SLOW_SPEED_DEG_PER_SEC, force)) {
        return false;
    }
    currentPosition = PEN_UP_ANGLE;
    return true;
}

bool Pen::queueSlowDown(bool force) {
    if (penDistance == -1) {
        return false;
    }
    if (!bridge->queueSlowServo(penDistance, PEN_SLOW_SPEED_DEG_PER_SEC, force)) {
        return false;
    }
    currentPosition = penDistance;
    return true;
}

void Pen::immediateUp() {
    bridge->setServoImmediate(PEN_UP_ANGLE);
    currentPosition = PEN_UP_ANGLE;
}
