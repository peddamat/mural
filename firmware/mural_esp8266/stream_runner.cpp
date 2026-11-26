#include "stream_runner.h"
#include <stdlib.h>
#include <string.h>

StreamRunner::StreamRunner(Movement* movement, Pen* pen, Display* display)
    : movement(movement),
      pen(pen),
      display(display),
      bufHead(0),
      bufTail(0),
      bufCount(0),
      state(STATE_IDLE),
      speed(PRINT_SPEED_STEPS),
      streamDone(false),
      finishPenLifted(false),
      nextExpectedSeq(0),
      executedCount(0),
      totalCommands(0),
      totalDistance(0.0),
      distanceSoFar(0.0),
      progress(-1),
      hasActiveMove(false),
      moveOrigin(0, 0),
      moveTarget(0, 0) {}

void StreamRunner::reset() {
    bufHead = 0;
    bufTail = 0;
    bufCount = 0;
    state = STATE_IDLE;
    streamDone = false;
    finishPenLifted = false;
    nextExpectedSeq = 0;
    executedCount = 0;
    totalCommands = 0;
    totalDistance = 0.0;
    distanceSoFar = 0.0;
    progress = -1;
    hasActiveMove = false;
}

bool StreamRunner::start(int drawSpeed, double totalDistMm, uint32_t totalCmds) {
    if (!movement->isHomed() || !movement->hasValidCoordinates()) {
        return false;
    }

    reset();
    speed = constrain(drawSpeed, MIN_DRAW_SPEED, MAX_DRAW_SPEED);
    totalDistance = totalDistMm;
    totalCommands = totalCmds;
    state = STATE_DRAWING;
    progress = 0;
    display->showDrawing(0);
    return true;
}

void StreamRunner::pause() {
    if (state == STATE_DRAWING) {
        state = STATE_PAUSED;
        display->showPaused(getProgress());
    }
}

void StreamRunner::resume() {
    if (state == STATE_PAUSED) {
        state = STATE_DRAWING;
        display->showDrawing(getProgress());
    }
}

void StreamRunner::stop() {
    movement->stopAll();
    pen->immediateUp();
    bufHead = 0;
    bufTail = 0;
    bufCount = 0;
    hasActiveMove = false;
    state = STATE_STOPPED;
    display->showFinished();
}

const char* StreamRunner::getStateName() const {
    switch (state) {
        case STATE_IDLE:     return "idle";
        case STATE_DRAWING:  return "drawing";
        case STATE_PAUSED:   return "paused";
        case STATE_HOMING:   return "homing";
        case STATE_FINISHED: return "finished";
        case STATE_STOPPED:  return "stopped";
        default:             return "idle";
    }
}

bool StreamRunner::parseCommandLine(const char* line, StreamCmd& outCmd) {
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0') {
        return false;
    }

    // Header metadata lines if accidentally included: consume cleanly
    if (line[0] == 'd' || line[0] == 'D') {
        double d = strtod(line + 1, nullptr);
        if (d > 0.0 && totalDistance <= 0.0) {
            totalDistance = d;
        }
        return false;
    }
    if (line[0] == 'h' || line[0] == 'H') {
        return false;
    }

    if (line[0] == 'p' || line[0] == 'P') {
        outCmd.type = (line[1] == '1') ? CMD_PEN_DOWN : CMD_PEN_UP;
        outCmd.x = 0.0f;
        outCmd.y = 0.0f;
        return true;
    }

    char* endX = nullptr;
    double x = strtod(line, &endX);
    if (endX == line) {
        return false;
    }
    char* endY = nullptr;
    double y = strtod(endX, &endY);
    if (endY == endX) {
        return false;
    }

    outCmd.type = CMD_MOVE;
    outCmd.x = static_cast<float>(x);
    outCmd.y = static_cast<float>(y);
    return true;
}

int StreamRunner::ingestBatch(uint32_t batchSeq, const String& payload, bool isFinalBatch) {
    if (state != STATE_DRAWING && state != STATE_PAUSED) {
        return -1;
    }
    if (batchSeq > nextExpectedSeq) {
        // Gap detected — client should rewind to nextExpectedSeq
        return -1;
    }

    int accepted = 0;
    uint32_t currentSeq = batchSeq;

    int start = 0;
    const int totalLen = payload.length();
    bool stoppedEarlyForFullBuffer = false;

    while (start <= totalLen) {
        int nl = payload.indexOf('\n', start);
        String lineStr = (nl == -1) ? payload.substring(start) : payload.substring(start, nl);
        lineStr.trim();

        if (lineStr.length() > 0) {
            if (currentSeq < nextExpectedSeq) {
                // Already received on an earlier attempt — skip idempotently
                currentSeq++;
            } else {
                if (bufCount >= STREAM_BUFFER_CAPACITY) {
                    stoppedEarlyForFullBuffer = true;
                    break;
                }

                StreamCmd cmd;
                if (parseCommandLine(lineStr.c_str(), cmd)) {
                    buffer[bufTail] = cmd;
                    bufTail = (bufTail + 1) % STREAM_BUFFER_CAPACITY;
                    bufCount++;
                }
                // Advance sequence even for metadata lines so browser indices stay aligned
                nextExpectedSeq++;
                currentSeq++;
                accepted++;
            }
        }

        if (nl == -1) {
            break;
        }
        start = nl + 1;
    }

    if (isFinalBatch && !stoppedEarlyForFullBuffer) {
        streamDone = true;
    }

    return accepted;
}

void StreamRunner::updateProgress(double addedDist) {
    distanceSoFar += addedDist;
    int newProgress = progress;

    if (totalDistance > 0.0) {
        newProgress = static_cast<int>(floor((distanceSoFar / totalDistance) * 100.0));
    } else if (totalCommands > 0) {
        newProgress = static_cast<int>((static_cast<uint64_t>(executedCount) * 100ULL) / totalCommands);
    }

    if (newProgress < 0) newProgress = 0;
    if (newProgress > 100) newProgress = 100;

    if (newProgress != progress) {
        progress = newProgress;
        display->showDrawing(progress);
    }
}

void StreamRunner::advanceInterpolation() {
    if (!hasActiveMove || !movement->canQueueMove()) {
        return;
    }

    Movement::Point current = movement->getCoordinates();
    double dist = Movement::distanceBetweenPoints(current, moveTarget);

    if (dist <= INTERPOLATION_INCREMENT_MM) {
        // canQueueMove() is already true; whether accepted or out-of-bounds, finish this segment
        // so a bad coordinate can never stall the runner indefinitely.
        movement->beginLinearTravel(moveTarget.x, moveTarget.y, speed);
        hasActiveMove = false;
        if (state == STATE_DRAWING) {
            executedCount++;
            updateProgress(Movement::distanceBetweenPoints(moveOrigin, moveTarget));
        }
        return;
    }

    double nextX = current.x + (INTERPOLATION_INCREMENT_MM / dist) * (moveTarget.x - current.x);
    double nextY = current.y + (INTERPOLATION_INCREMENT_MM / dist) * (moveTarget.y - current.y);
    if (!movement->beginLinearTravel(nextX, nextY, speed)) {
        hasActiveMove = false;
        if (state == STATE_DRAWING) {
            executedCount++;
        }
    }
}

void StreamRunner::run() {
    if (state == STATE_DRAWING) {
        // 1. Continue feeding interpolated 1mm segments of current move
        if (hasActiveMove) {
            advanceInterpolation();
            return;
        }

        // 2. Pop next buffered command if ATmega328P queue has room
        if (bufCount > 0) {
            if (!movement->canQueueMove()) {
                return;
            }

            StreamCmd cmd = buffer[bufHead];
            if (cmd.type == CMD_PEN_UP) {
                if (pen->queueSlowUp()) {
                    bufHead = (bufHead + 1) % STREAM_BUFFER_CAPACITY;
                    bufCount--;
                    executedCount++;
                }
                return;
            }
            if (cmd.type == CMD_PEN_DOWN) {
                if (pen->queueSlowDown()) {
                    bufHead = (bufHead + 1) % STREAM_BUFFER_CAPACITY;
                    bufCount--;
                    executedCount++;
                }
                return;
            }

            // CMD_MOVE
            bufHead = (bufHead + 1) % STREAM_BUFFER_CAPACITY;
            bufCount--;

            moveOrigin = movement->getCoordinates();
            moveTarget = Movement::Point(static_cast<double>(cmd.x), static_cast<double>(cmd.y));
            hasActiveMove = true;
            advanceInterpolation();
            return;
        }

        // 3. All streamed commands executed -> lift pen and return home
        if (streamDone) {
            if (!finishPenLifted) {
                if (movement->canQueueMove()) {
                    pen->queueSlowUp(true);
                    finishPenLifted = true;
                }
                return;
            }

            moveOrigin = movement->getCoordinates();
            moveTarget = movement->getHomeCoordinates();
            hasActiveMove = true;
            state = STATE_HOMING;
        }
    } else if (state == STATE_HOMING) {
        if (hasActiveMove) {
            advanceInterpolation();
            return;
        }
        if (!movement->isMoving()) {
            progress = 100;
            state = STATE_FINISHED;
            display->showFinished();
        }
    }
}
