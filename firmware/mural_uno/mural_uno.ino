// ============================================================================
// Mural v2 - ATmega328P Motion Controller Firmware
// Target: HW-649 (UNO+WiFi R3 ATmega328P+ESP8266) + Arduino CNC Shield V3.0
//
// Responsibilities:
//   - Drives Left (X-axis) and Right (Y-axis) steppers via TMC2209 (standalone
//     STEP/DIR mode, no MS jumpers = 1/8 microstepping) & AccelStepper
//   - Drives MG90S pen servo on D11 (CNC Shield V3.0 Z+ endstop pin)
//   - Optional 0.91" SSD1306 I2C OLED display on A4 (SDA) / A5 (SCL)
//   - Maintains a 10-slot FIFO motion/servo queue fed by ESP8266 over UART
//
// HW-649 DIP Switch Settings:
//   - Flash this sketch (ATmega328P): SW 3,4 ON (all others OFF)
//   - Normal operation (with ESP8266): SW 1,2 ON (all others OFF)
//
// Protocol (57600 baud, newline-terminated):
//   ESP8266 -> Uno (all commands start with '$'):
//     $?                          Query status
//     $P <posL> <posR>            Set absolute stepper positions (clears queue)
//     $M <posL> <posR> <sL> <sR>  Enqueue moveTo(posL, posR) at speeds sL, sR
//     $R <steps> <speed>          Relative move both motors (e-steps calibration)
//     $L <dir> <speed>            Continuous jog left motor (-1, 0, +1)
//     $K <dir> <speed>            Continuous jog right motor (-1, 0, +1)
//     $X                          Emergency stop & clear queue
//     $S <angle>                  Immediate raw servo angle (0-180)
//     $V <angle> <degPerSec>      Enqueue smooth slow pen move (+200ms settle)
//     $O <line1>|<line2>          Show 1 or 2 lines on I2C OLED
//     $B <percent>                Show drawing progress bar on I2C OLED
//
//   Uno -> ESP8266 (all replies start with '@'):
//     @Q <freeSlots> <busy> <posL> <posR>
// ============================================================================

#include <Arduino.h>
#include <AccelStepper.h>
#include <Servo.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "oled_display.h"

namespace {

constexpr long INFINITE_STEPS = 999999999L;

enum MotionType : uint8_t {
    MOTION_MOVE_TO    = 0,
    MOTION_SERVO_SLOW = 1
};

struct MotionItem {
    uint8_t type;
    int32_t leftTarget;   // MOVE_TO: target left steps  | SERVO_SLOW: target angle
    int32_t rightTarget;  // MOVE_TO: target right steps | SERVO_SLOW: deg/sec
    float leftSpeed;
    float rightSpeed;
};

AccelStepper leftMotor(AccelStepper::DRIVER, LEFT_STEP_PIN, LEFT_DIR_PIN);
AccelStepper rightMotor(AccelStepper::DRIVER, RIGHT_STEP_PIN, RIGHT_DIR_PIN);
Servo penServo;
OledDisplay oled;

// FIFO motion queue
MotionItem queue[MOTION_QUEUE_SIZE];
uint8_t qHead = 0;
uint8_t qTail = 0;
uint8_t qCount = 0;

// Active execution state
bool steppersActive = false;
bool jogActive = false;

// Non-blocking slow servo state
bool servoMoving = false;
int servoCurrentAngle = SERVO_INITIAL_ANGLE;
int servoStartAngle = SERVO_INITIAL_ANGLE;
int servoTargetAngle = SERVO_INITIAL_ANGLE;
int servoSpeedDegPerSec = 90;
unsigned long servoStartMs = 0;
unsigned long servoLastUpdateMs = 0;
bool servoSettling = false;
unsigned long servoSettleStartMs = 0;

// Serial receive buffer
char rxBuf[80];
uint8_t rxLen = 0;

// Heartbeat timer
unsigned long lastStatusMs = 0;

void serviceSteppersOnly() {
    if (steppersActive) {
        leftMotor.runSpeedToPosition();
        rightMotor.runSpeedToPosition();
    }
}

uint8_t queueFreeSlots() {
    return MOTION_QUEUE_SIZE - qCount;
}

bool isBusy() {
    return steppersActive || jogActive || servoMoving || (qCount > 0);
}

void sendStatus() {
    Serial.print(F("@Q "));
    Serial.print(queueFreeSlots());
    Serial.print(' ');
    Serial.print(isBusy() ? 1 : 0);
    Serial.print(' ');
    Serial.print(leftMotor.currentPosition());
    Serial.print(' ');
    Serial.println(rightMotor.currentPosition());
    lastStatusMs = millis();
}

void clearQueue() {
    qHead = 0;
    qTail = 0;
    qCount = 0;
}

void stopAllMotion() {
    clearQueue();
    leftMotor.moveTo(leftMotor.currentPosition());
    leftMotor.setSpeed(0);
    rightMotor.moveTo(rightMotor.currentPosition());
    rightMotor.setSpeed(0);
    steppersActive = false;
    jogActive = false;
    servoMoving = false;
    servoSettling = false;
}

bool enqueueItem(const MotionItem& item) {
    if (qCount >= MOTION_QUEUE_SIZE) {
        return false;
    }
    queue[qTail] = item;
    qTail = (qTail + 1) % MOTION_QUEUE_SIZE;
    qCount++;
    return true;
}

void startNextQueuedItem() {
    if (qCount == 0 || steppersActive || jogActive || servoMoving) {
        return;
    }

    MotionItem item = queue[qHead];
    qHead = (qHead + 1) % MOTION_QUEUE_SIZE;
    qCount--;

    if (item.type == MOTION_MOVE_TO) {
        leftMotor.moveTo(item.leftTarget);
        leftMotor.setSpeed(item.leftSpeed);
        rightMotor.moveTo(item.rightTarget);
        rightMotor.setSpeed(item.rightSpeed);
        steppersActive = (leftMotor.distanceToGo() != 0 || rightMotor.distanceToGo() != 0);
        if (!steppersActive) {
            // Zero-length move: immediately notify & advance
            sendStatus();
        }
    } else if (item.type == MOTION_SERVO_SLOW) {
        int target = constrain(static_cast<int>(item.leftTarget), 0, 180);
        int speed = max(1, static_cast<int>(item.rightTarget));
        if (target == servoCurrentAngle) {
            sendStatus();
            return;
        }
        servoStartAngle = servoCurrentAngle;
        servoTargetAngle = target;
        servoSpeedDegPerSec = speed;
        servoStartMs = millis();
        servoLastUpdateMs = servoStartMs;
        servoMoving = true;
        servoSettling = false;
    }
}

void serviceServo() {
    if (!servoMoving) {
        return;
    }

    unsigned long now = millis();
    if (servoSettling) {
        if (now - servoSettleStartMs >= 200) {
            servoSettling = false;
            servoMoving = false;
            sendStatus();
        }
        return;
    }

    if (now - servoLastUpdateMs < 10) {
        return;
    }
    servoLastUpdateMs = now;

    unsigned long elapsed = now - servoStartMs;
    long delta = static_cast<long>((static_cast<double>(elapsed) / 1000.0) * servoSpeedDegPerSec);
    int nextAngle;
    bool reached;

    if (servoTargetAngle > servoStartAngle) {
        nextAngle = servoStartAngle + static_cast<int>(delta);
        reached = (nextAngle >= servoTargetAngle);
    } else {
        nextAngle = servoStartAngle - static_cast<int>(delta);
        reached = (nextAngle <= servoTargetAngle);
    }

    if (reached) {
        servoCurrentAngle = servoTargetAngle;
        penServo.write(servoCurrentAngle);
        servoSettling = true;
        servoSettleStartMs = now;
    } else if (nextAngle != servoCurrentAngle) {
        servoCurrentAngle = nextAngle;
        penServo.write(servoCurrentAngle);
    }
}

void handleCommandLine(char* line) {
    // Only process framed commands starting with '$' (ignores ESP8266 boot chatter)
    if (line[0] != '$') {
        return;
    }

    char cmd = line[1];
    char* args = (line[2] == ' ') ? (line + 3) : (line + 2);

    switch (cmd) {
        case '?': {
            sendStatus();
            break;
        }

        case 'P': {
            // $P <posL> <posR>
            char* end1 = nullptr;
            long posL = strtol(args, &end1, 10);
            long posR = strtol(end1, nullptr, 10);
            stopAllMotion();
            leftMotor.setCurrentPosition(posL);
            rightMotor.setCurrentPosition(posR);
            sendStatus();
            break;
        }

        case 'M': {
            // $M <posL> <posR> <spdL> <spdR>
            char* p = args;
            long posL = strtol(p, &p, 10);
            long posR = strtol(p, &p, 10);
            float spdL = static_cast<float>(strtod(p, &p));
            float spdR = static_cast<float>(strtod(p, &p));

            if (spdL < 0.0f) spdL = -spdL;
            if (spdR < 0.0f) spdR = -spdR;
            if (spdL > MAX_STEPPER_SPEED) spdL = MAX_STEPPER_SPEED;
            if (spdR > MAX_STEPPER_SPEED) spdR = MAX_STEPPER_SPEED;

            MotionItem item;
            item.type = MOTION_MOVE_TO;
            item.leftTarget = posL;
            item.rightTarget = posR;
            item.leftSpeed = spdL;
            item.rightSpeed = spdR;
            enqueueItem(item);
            startNextQueuedItem();
            sendStatus();
            break;
        }

        case 'R': {
            // $R <steps> <speed>  (e-steps calibration relative extend)
            char* p = args;
            long steps = strtol(p, &p, 10);
            float spd = static_cast<float>(strtod(p, &p));
            if (spd < 1.0f) spd = 650.0f;
            if (spd > MAX_STEPPER_SPEED) spd = MAX_STEPPER_SPEED;

            stopAllMotion();
            leftMotor.move(steps);
            leftMotor.setSpeed(spd);
            rightMotor.move(steps);
            rightMotor.setSpeed(spd);
            steppersActive = (steps != 0);
            sendStatus();
            break;
        }

        case 'L': {
            // $L <dir> <speed>  (jog left motor: -1, 0, +1)
            char* p = args;
            int dir = static_cast<int>(strtol(p, &p, 10));
            float spd = static_cast<float>(strtod(p, &p));
            if (spd < 1.0f) spd = 650.0f;
            if (spd > MAX_STEPPER_SPEED) spd = MAX_STEPPER_SPEED;

            clearQueue();
            if (dir > 0) {
                leftMotor.move(INFINITE_STEPS);
                leftMotor.setSpeed(spd);
            } else if (dir < 0) {
                leftMotor.move(-INFINITE_STEPS);
                leftMotor.setSpeed(spd);
            } else {
                leftMotor.moveTo(leftMotor.currentPosition());
                leftMotor.setSpeed(0);
            }
            jogActive = (leftMotor.distanceToGo() != 0 || rightMotor.distanceToGo() != 0);
            steppersActive = jogActive;
            sendStatus();
            break;
        }

        case 'K': {
            // $K <dir> <speed>  (jog right motor: -1, 0, +1)
            char* p = args;
            int dir = static_cast<int>(strtol(p, &p, 10));
            float spd = static_cast<float>(strtod(p, &p));
            if (spd < 1.0f) spd = 650.0f;
            if (spd > MAX_STEPPER_SPEED) spd = MAX_STEPPER_SPEED;

            clearQueue();
            if (dir > 0) {
                rightMotor.move(INFINITE_STEPS);
                rightMotor.setSpeed(spd);
            } else if (dir < 0) {
                rightMotor.move(-INFINITE_STEPS);
                rightMotor.setSpeed(spd);
            } else {
                rightMotor.moveTo(rightMotor.currentPosition());
                rightMotor.setSpeed(0);
            }
            jogActive = (leftMotor.distanceToGo() != 0 || rightMotor.distanceToGo() != 0);
            steppersActive = jogActive;
            sendStatus();
            break;
        }

        case 'X': {
            // Immediate stop & clear queue
            stopAllMotion();
            sendStatus();
            break;
        }

        case 'S': {
            // Immediate raw servo write (calibration / park)
            int angle = constrain(atoi(args), 0, 180);
            servoMoving = false;
            servoSettling = false;
            servoCurrentAngle = angle;
            penServo.write(angle);
            sendStatus();
            break;
        }

        case 'V': {
            // Enqueue slow pen move: $V <targetAngle> <degPerSec>
            char* p = args;
            int angle = constrain(static_cast<int>(strtol(p, &p, 10)), 0, 180);
            int speed = static_cast<int>(strtol(p, &p, 10));
            if (speed <= 0) speed = 90;

            MotionItem item;
            item.type = MOTION_SERVO_SLOW;
            item.leftTarget = angle;
            item.rightTarget = speed;
            item.leftSpeed = 0;
            item.rightSpeed = 0;
            enqueueItem(item);
            startNextQueuedItem();
            sendStatus();
            break;
        }

        case 'O': {
            // $O <line1>|<line2>
            char* sep = strchr(args, '|');
            if (sep) {
                *sep = '\0';
                oled.showLines(args, sep + 1);
            } else {
                oled.showLines(args, nullptr);
            }
            break;
        }

        case 'B': {
            // $B <percent>
            int pct = atoi(args);
            oled.showProgress(pct);
            break;
        }

        default:
            break;
    }
}

void serviceSerial() {
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            rxBuf[rxLen] = '\0';
            if (rxLen > 0) {
                handleCommandLine(rxBuf);
            }
            rxLen = 0;
            // Service steppers right after parsing one line
            serviceSteppersOnly();
            break;
        } else if (rxLen < sizeof(rxBuf) - 1) {
            rxBuf[rxLen++] = c;
        } else {
            // Overflow protection: reset line
            rxLen = 0;
        }
    }
}

} // namespace

void setup() {
    // Enable TMC2209 drivers right away (Active LOW) so belts stay taut and
    // motors don't creep or drop the carriage on power-up.
    pinMode(MOTOR_ENABLE_PIN, OUTPUT);
    digitalWrite(MOTOR_ENABLE_PIN, LOW);

    leftMotor.setMaxSpeed(MAX_STEPPER_SPEED);
    leftMotor.setPinsInverted(INVERT_LEFT_DIR, false, false);

    rightMotor.setMaxSpeed(MAX_STEPPER_SPEED);
    rightMotor.setPinsInverted(INVERT_RIGHT_DIR, false, false);

    penServo.attach(PEN_SERVO_PIN);
    penServo.write(SERVO_INITIAL_ANGLE);
    servoCurrentAngle = SERVO_INITIAL_ANGLE;

    Serial.begin(LINK_BAUD_RATE);

    if (oled.begin(serviceSteppersOnly)) {
        oled.showLines("Mural v2 UNO", "Waiting WiFi...");
    }

    sendStatus();
}

void loop() {
    // 1. Highest priority: keep steppers stepping accurately
    if (steppersActive) {
        leftMotor.runSpeedToPosition();
        rightMotor.runSpeedToPosition();

        if (!jogActive && leftMotor.distanceToGo() == 0 && rightMotor.distanceToGo() == 0) {
            steppersActive = false;
            startNextQueuedItem();
            sendStatus();
        }
    } else if (qCount > 0 && !servoMoving) {
        startNextQueuedItem();
    }

    // 2. Non-blocking smooth pen servo
    serviceServo();

    // 3. Process incoming ESP8266 serial commands
    serviceSerial();

    // 4. Periodic heartbeat so ESP8266 always has fresh position & queue state
    unsigned long now = millis();
    unsigned long interval = isBusy() ? STATUS_HEARTBEAT_MOVING_MS : STATUS_HEARTBEAT_IDLE_MS;
    if (now - lastStatusMs >= interval) {
        sendStatus();
    }
}
