#ifndef MURAL_ESP8266_MOVEMENT_H
#define MURAL_ESP8266_MOVEMENT_H

#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "uno_bridge.h"
#include "display.h"

class Movement {
public:
    struct Point {
        double x;
        double y;
        Point(double xVal, double yVal) : x(xVal), y(yVal) {}
        Point() : x(0), y(0) {}
    };

    Movement(UnoBridge* bridge, Display* display);

    static double distanceBetweenPoints(Point p1, Point p2) {
        double dx = p2.x - p1.x;
        double dy = p2.y - p1.y;
        return sqrt(dx * dx + dy * dy);
    }

    bool isMoving() const;
    bool hasStartedHoming() const { return startedHoming; }
    bool isHomed() const { return homed; }
    bool canQueueMove() const { return bridge->canEnqueue(); }

    double getWidth() const { return (topDistance == -1) ? -1.0 : width; }
    bool hasValidCoordinates() const { return X >= 0 && Y >= 0; }
    Point getCoordinates() const { return Point(X, Y); }
    Point getHomeCoordinates() const;

    void setTopDistance(int distance);
    void resumeTopDistance(int distance);
    int getTopDistance() const { return topDistance; }

    double getPulleyDiameter() const { return diameter; }
    void setPulleyDiameter(double newDiameter);

    void leftStepper(int dir);
    void rightStepper(int dir);
    void stopAll();

    // Sets retracted origin and queues travel to home. Returns estimated seconds.
    int extendToHome();

    // Computes belt lengths via 64-bit kinematic model and queues move on ATmega328P.
    // Returns true if queued (or zero-distance no-op), false if invalid coords or queue full.
    bool beginLinearTravel(double x, double y, int speed, float* outMoveTime = nullptr, bool force = false);

    // Extends both belts by distanceMm for e-steps calibration.
    void extendBelts(int distanceMm);

    // Resets homing/position flags when restarting workflow.
    void resetSession();

private:
    struct Lengths {
        int32_t left;
        int32_t right;
        Lengths(int32_t l, int32_t r) : left(l), right(r) {}
        Lengths() : left(0), right(0) {}
    };

    UnoBridge* bridge;
    Display* display;

    int topDistance;
    double diameter;
    double circumference;
    double d_t;
    double d_p;
    double d_m;
    double minSafeY;
    double minSafeXOffset;
    double width;

    bool homed;
    bool startedHoming;
    double X;
    double Y;

    // Tracks planned step target across pipelined Uno queue moves
    int32_t plannedLeftSteps;
    int32_t plannedRightSteps;

    double gamma_last_position;

    void updatePulleyGeometry();
    void loadPulleyDiameter();
    void savePulleyDiameter();
    void setOrigin();

    Lengths getBeltLengths(double x, double y);
    inline void getLeftTangentPoint(double frameX, double frameY, double gamma, double& x_PL, double& y_PL) const;
    inline void getRightTangentPoint(double frameX, double frameY, double gamma, double& x_PR, double& y_PR) const;
    void getBeltAngles(double frameX, double frameY, double gamma, double& phi_L, double& phi_R) const;
    void getBeltForces(double phi_L, double phi_R, double& F_L, double& F_R) const;
    double solveTorqueEquilibrium(double phi_L, double phi_R, double F_L, double F_R, double gamma_init) const;
    inline double getDilationCorrectedBeltLength(double belt_length, double F_belt) const;
};

#endif // MURAL_ESP8266_MOVEMENT_H
