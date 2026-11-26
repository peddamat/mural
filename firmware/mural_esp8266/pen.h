#ifndef MURAL_ESP8266_PEN_H
#define MURAL_ESP8266_PEN_H

#include "uno_bridge.h"

class Pen {
public:
    explicit Pen(UnoBridge* bridge);

    void setRawValue(int rawValue);
    void setPenDistance(int value);
    int getPenDistance() const { return penDistance; }

    // Enqueues smooth pen moves in the Uno's motion FIFO so pen up/down stays
    // perfectly synchronized with queued stepper segments.
    bool queueSlowUp(bool force = false);
    bool queueSlowDown(bool force = false);

    // Immediate non-queued lift (used on emergency stop)
    void immediateUp();

    bool isDown() const { return currentPosition == penDistance && penDistance != -1; }

private:
    UnoBridge* bridge;
    int penDistance;
    int currentPosition;
};

#endif // MURAL_ESP8266_PEN_H
