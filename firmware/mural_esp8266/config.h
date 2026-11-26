#ifndef MURAL_ESP8266_CONFIG_H
#define MURAL_ESP8266_CONFIG_H

#include <Arduino.h>

// ============================================================================
// Serial Link Configuration (ESP8266 <-> ATmega328P on HW-649)
// Set HW-649 DIP switches 1 & 2 ON (all others OFF) for normal operation.
// ============================================================================
constexpr unsigned long LINK_BAUD_RATE = 57600;

// ============================================================================
// Motor & Microstepping Parameters
// Configure A4988 drivers on CNC Shield V3.0 for 1/8 microstepping:
//   MS1 = Jumper ON, MS2 = Jumper ON, MS3 = Jumper OFF (200 * 8 = 1600 steps/rev)
// ============================================================================
constexpr int MICROSTEPS = 8;
constexpr int STEPS_PER_ROTATION = 200 * MICROSTEPS;

constexpr int PRINT_SPEED_STEPS = 650;
constexpr int MOVE_SPEED_STEPS  = 1500;
constexpr int MIN_DRAW_SPEED    = 300;
constexpr int MAX_DRAW_SPEED    = 1800;

// ============================================================================
// Mural v2 Mechanical Geometry Parameters (identical to KinematicModel.md)
// ============================================================================
constexpr double DEFAULT_DIAMETER = 10.14926;          // [mm] effective pulley+belt diameter
constexpr double MID_PULLEY_TO_WALL = 41.0;            // [mm] pulley center to wall standoff
constexpr double HOMED_STEP_OFFSET_MM = 40.0;          // [mm] fully retracted belt length to stop screw
constexpr double MASS_BOT = 0.55;                      // [kg] mass of carriage
constexpr double G_CONSTANT = 9.81;                    // [m/s^2] gravity
constexpr double PULLEY_AXIS_DISTANCE = 89.50;         // [mm] distance between pulley axes
constexpr double AXES_TO_CENTER_OF_MASS = 10.0;        // [mm] pulley axes line to COM
constexpr double BELT_ELONGATION_COEFFICIENT = 5e-5;   // [m/N] belt stretch under tension
constexpr int HOME_Y_OFFSET_MM = 350;                  // [mm] Y coordinate of home position

constexpr double SAFE_Y_FRACTION = 0.25;               // Top margin fraction of topDistance
constexpr double SAFE_X_FRACTION = 0.20;               // Left/right margin fraction of topDistance

// Linear interpolation step size [mm] (keeps lines straight in polar kinematics)
constexpr double INTERPOLATION_INCREMENT_MM = 1.0;

// ============================================================================
// Browser Streaming Buffer Configuration
// ============================================================================
constexpr uint16_t STREAM_BUFFER_CAPACITY = 64;
constexpr int PEN_SLOW_SPEED_DEG_PER_SEC = 90;
constexpr int PEN_UP_ANGLE = 90;
constexpr int PEN_INIT_ANGLE = 70;

#endif // MURAL_ESP8266_CONFIG_H
