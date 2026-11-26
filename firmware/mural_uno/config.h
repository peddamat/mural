#ifndef MURAL_UNO_CONFIG_H
#define MURAL_UNO_CONFIG_H

#include <Arduino.h>

// ============================================================================
// Hardware Pinout: HW-649 (ATmega328P) + Arduino CNC Shield V3.0 + A4988
// ============================================================================

// Left stepper motor -> CNC Shield V3.0 X-Axis slot
constexpr uint8_t LEFT_STEP_PIN = 2;
constexpr uint8_t LEFT_DIR_PIN  = 5;

// Right stepper motor -> CNC Shield V3.0 Y-Axis slot
constexpr uint8_t RIGHT_STEP_PIN = 3;
constexpr uint8_t RIGHT_DIR_PIN  = 6;

// Shared stepper driver enable pin on CNC Shield V3.0 (Active LOW: LOW = enabled)
constexpr uint8_t MOTOR_ENABLE_PIN = 8;

// MG90S Pen Servo -> CNC Shield V3.0 Z+ (or Z-) endstop header = Arduino D11
// (Note: On CNC Shield V3.0 silkscreen, Z+ / Z- connects to D11, SpnEn to D12.)
constexpr uint8_t PEN_SERVO_PIN = 11;

// Initial servo angle on boot (pen retracted away from wall)
constexpr int SERVO_INITIAL_ANGLE = 70;

// Direction inversion: Mural mounts left & right motors mirrored.
// If a motor spins backwards during testing, flip the corresponding bool here.
constexpr bool INVERT_LEFT_DIR  = true;
constexpr bool INVERT_RIGHT_DIR = false;

// ============================================================================
// Motion & Serial Configuration
// ============================================================================

// Hardware UART between ATmega328P and ESP8266 (HW-649 DIP switches 1 & 2 ON).
// 57600 baud has <0.8% clock error on 16 MHz ATmega328P (with U2X=1) and
// transmits ~250+ motion commands per second cleanly.
constexpr unsigned long LINK_BAUD_RATE = 57600;

// Maximum stepper speed (steps/sec) on 16 MHz ATmega328P with AccelStepper
constexpr float MAX_STEPPER_SPEED = 2000.0f;

// Number of queued motion / pen commands buffered locally on ATmega328P.
// Keeps steppers moving smoothly with zero stutter even during ESP8266 WiFi bursts.
constexpr uint8_t MOTION_QUEUE_SIZE = 10;

// Period (ms) for unsolicited heartbeat/status updates while moving or idle
constexpr unsigned long STATUS_HEARTBEAT_MOVING_MS = 120;
constexpr unsigned long STATUS_HEARTBEAT_IDLE_MS   = 500;

// ============================================================================
// Optional 0.91" I2C OLED Display (SSD1306 128x32 on A4=SDA, A5=SCL)
// ============================================================================
constexpr uint8_t OLED_I2C_ADDR = 0x3C;

#endif // MURAL_UNO_CONFIG_H
