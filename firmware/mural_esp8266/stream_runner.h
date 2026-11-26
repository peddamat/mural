#ifndef MURAL_ESP8266_STREAM_RUNNER_H
#define MURAL_ESP8266_STREAM_RUNNER_H

#include <Arduino.h>
#include "config.h"
#include "movement.h"
#include "pen.h"
#include "display.h"

class StreamRunner {
public:
    enum State : uint8_t {
        STATE_IDLE     = 0,
        STATE_DRAWING  = 1,
        STATE_PAUSED   = 2,
        STATE_HOMING   = 3,
        STATE_FINISHED = 4,
        STATE_STOPPED  = 5
    };

    StreamRunner(Movement* movement, Pen* pen, Display* display);

    // Begins a new browser-streamed drawing session.
    bool start(int drawSpeed, double totalDistMm, uint32_t totalCmds);

    // Ingests a batch of newline-separated commands starting at `batchSeq`.
    // Idempotent on retries: commands with seq < nextExpectedSeq are skipped.
    // Returns number of newly enqueued commands (or -1 if seq > nextExpectedSeq).
    int ingestBatch(uint32_t batchSeq, const String& payload, bool isFinalBatch);

    void pause();
    void resume();
    void stop();
    void reset();

    // Called continuously from loop()
    void run();

    // Telemetry for browser backpressure & progress UI
    State getState() const { return state; }
    const char* getStateName() const;
    uint32_t getNextExpectedSeq() const { return nextExpectedSeq; }
    uint32_t getExecutedCount() const { return executedCount; }
    uint32_t getTotalCommands() const { return totalCommands; }
    uint16_t getBufferedCount() const { return bufCount; }
    uint16_t getBufferCapacity() const { return STREAM_BUFFER_CAPACITY; }
    int getProgress() const { return (progress < 0) ? 0 : progress; }
    bool isStreamDone() const { return streamDone; }

private:
    enum CmdType : uint8_t {
        CMD_MOVE     = 0,
        CMD_PEN_UP   = 1,
        CMD_PEN_DOWN = 2
    };

    struct StreamCmd {
        uint8_t type;
        float x;
        float y;
    };

    Movement* movement;
    Pen* pen;
    Display* display;

    // Static ring buffer — zero heap allocations during drawing
    StreamCmd buffer[STREAM_BUFFER_CAPACITY];
    uint16_t bufHead;
    uint16_t bufTail;
    uint16_t bufCount;

    State state;
    int speed;
    bool streamDone;
    bool finishPenLifted;

    uint32_t nextExpectedSeq;
    uint32_t executedCount;
    uint32_t totalCommands;

    double totalDistance;
    double distanceSoFar;
    int progress;

    // Active linear interpolation target
    bool hasActiveMove;
    Movement::Point moveOrigin;
    Movement::Point moveTarget;

    bool parseCommandLine(const char* line, StreamCmd& outCmd);
    void advanceInterpolation();
    void updateProgress(double addedDist);
};

#endif // MURAL_ESP8266_STREAM_RUNNER_H
