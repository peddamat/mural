#include "movement.h"
#include <LittleFS.h>
#include <stdlib.h>

namespace {
constexpr const char* DIAMETER_FILE = "/pulley_diameter.txt";
} // namespace

Movement::Movement(UnoBridge* bridge, Display* display)
    : bridge(bridge),
      display(display),
      topDistance(-1),
      diameter(DEFAULT_DIAMETER),
      circumference(0),
      d_t(0),
      d_p(0),
      d_m(0),
      minSafeY(0),
      minSafeXOffset(0),
      width(0),
      homed(false),
      startedHoming(false),
      X(-1),
      Y(-1),
      plannedLeftSteps(0),
      plannedRightSteps(0),
      gamma_last_position(0.0) {
    loadPulleyDiameter();
    updatePulleyGeometry();
}

void Movement::loadPulleyDiameter() {
    diameter = DEFAULT_DIAMETER;
    if (!LittleFS.exists(DIAMETER_FILE)) {
        return;
    }
    File f = LittleFS.open(DIAMETER_FILE, "r");
    if (!f) {
        return;
    }
    String val = f.readStringUntil('\n');
    f.close();
    double parsed = strtod(val.c_str(), nullptr);
    if (parsed > 1.0 && parsed < 100.0) {
        diameter = parsed;
    }
}

void Movement::savePulleyDiameter() {
    File f = LittleFS.open(DIAMETER_FILE, "w");
    if (!f) {
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6f\n", diameter);
    f.print(buf);
    f.close();
}

void Movement::updatePulleyGeometry() {
    circumference = diameter * PI;
    const double q = diameter / (2.0 * sqrt(2.0));
    d_t = PULLEY_AXIS_DISTANCE - 2.0 * q;
    d_p = q;
    d_m = AXES_TO_CENTER_OF_MASS + q;
}

void Movement::setPulleyDiameter(double newDiameter) {
    if (newDiameter <= 1.0 || newDiameter >= 100.0) {
        return;
    }
    diameter = newDiameter;
    updatePulleyGeometry();
    savePulleyDiameter();
}

void Movement::setTopDistance(int distance) {
    topDistance = distance;
    minSafeY = SAFE_Y_FRACTION * topDistance;
    minSafeXOffset = SAFE_X_FRACTION * topDistance;
    width = topDistance - 2.0 * minSafeXOffset;
}

void Movement::resumeTopDistance(int distance) {
    setTopDistance(distance);
    homed = true;

    const Point home = getHomeCoordinates();
    X = home.x;
    Y = home.y;

    const Lengths lengths = getBeltLengths(home.x, home.y);
    plannedLeftSteps = lengths.left;
    plannedRightSteps = lengths.right;
    bridge->setPosition(lengths.left, lengths.right);
}

void Movement::resetSession() {
    stopAll();
    startedHoming = false;
    homed = false;
    X = -1;
    Y = -1;
    gamma_last_position = 0.0;
}

void Movement::setOrigin() {
    const int32_t homedStepsOffset = static_cast<int32_t>((HOMED_STEP_OFFSET_MM / circumference) * STEPS_PER_ROTATION);
    plannedLeftSteps = homedStepsOffset;
    plannedRightSteps = homedStepsOffset;
    bridge->setPosition(homedStepsOffset, homedStepsOffset);
    homed = true;
}

void Movement::leftStepper(int dir) {
    bridge->jogLeft(dir, static_cast<float>(PRINT_SPEED_STEPS));
}

void Movement::rightStepper(int dir) {
    bridge->jogRight(dir, static_cast<float>(PRINT_SPEED_STEPS));
}

void Movement::stopAll() {
    bridge->stopAll();
    plannedLeftSteps = bridge->getLeftPos();
    plannedRightSteps = bridge->getRightPos();
}

Movement::Point Movement::getHomeCoordinates() const {
    if (topDistance == -1) {
        return Point(0, 0);
    }
    return Point(width / 2.0, static_cast<double>(HOME_Y_OFFSET_MM));
}

int Movement::extendToHome() {
    setOrigin();
    Point home = getHomeCoordinates();
    startedHoming = true;
    float moveTime = 0.0f;
    beginLinearTravel(home.x, home.y, MOVE_SPEED_STEPS, &moveTime, true);
    return static_cast<int>(ceil(moveTime));
}

bool Movement::isMoving() const {
    return bridge->isBusy();
}

inline void Movement::getLeftTangentPoint(double frameX, double frameY, double gamma, double& x_PL, double& y_PL) const {
    const double s_L = d_t / 2.0;
    const double P_LX = s_L * cos(gamma) - d_p * sin(gamma);
    const double P_LY = s_L * sin(gamma) + d_p * cos(gamma);
    x_PL = frameX - P_LX;
    y_PL = frameY - P_LY;
}

inline void Movement::getRightTangentPoint(double frameX, double frameY, double gamma, double& x_PR, double& y_PR) const {
    const double s_R = d_t / 2.0;
    const double P_RX = s_R * cos(gamma) + d_p * sin(gamma);
    const double P_RY = s_R * sin(gamma) - d_p * cos(gamma);
    x_PR = frameX + P_RX;
    y_PR = frameY + P_RY;
}

void Movement::getBeltAngles(double frameX, double frameY, double gamma, double& phi_L, double& phi_R) const {
    double x_PL, y_PL;
    getLeftTangentPoint(frameX, frameY, gamma, x_PL, y_PL);
    phi_L = atan2(y_PL, x_PL);

    double x_PR, y_PR;
    getRightTangentPoint(frameX, frameY, gamma, x_PR, y_PR);
    phi_R = atan2(y_PR, topDistance - x_PR);
}

void Movement::getBeltForces(double phi_L, double phi_R, double& F_L, double& F_R) const {
    const double F_G = MASS_BOT * G_CONSTANT;
    const double denom = sin(phi_L + phi_R);
    if (fabs(denom) < 1e-6) {
        F_R = F_G;
        F_L = F_G;
        return;
    }
    F_R = F_G * cos(phi_L) / denom;
    F_L = F_G * cos(phi_R) / denom;
}

double Movement::solveTorqueEquilibrium(double phi_L, double phi_R, double F_L, double F_R, double gamma_init) const {
    const double s_L = d_t / 2.0;
    const double s_R = d_t / 2.0;

    double gamma_best = gamma_init;
    double T_delta_best = 99999999.0;

    constexpr double gamma_step = 0.20 * PI / 180.0;
    constexpr double gamma_min = -90.0 * PI / 180.0;
    constexpr double gamma_max = 90.0 * PI / 180.0;
    constexpr double gamma_search_window = 2.0 * PI / 180.0;

    for (double gamma = gamma_init - gamma_search_window;
         gamma > gamma_min && gamma < gamma_max && gamma <= gamma_init + gamma_search_window;
         gamma += gamma_step) {
        const double alpha = phi_L - gamma;
        const double beta = phi_R + gamma;

        const double T_L = s_L * sin(alpha) * F_L;
        const double T_R = s_R * sin(beta) * F_R;

        const double s_m = d_m * tan(gamma);
        const double F_G = MASS_BOT * G_CONSTANT;
        const double F_m = F_G * cos(gamma);
        const double T_m = s_m * F_m;

        const double T_delta = T_R - T_L + T_m;
        if (fabs(T_delta) < fabs(T_delta_best)) {
            T_delta_best = T_delta;
            gamma_best = gamma;
        } else {
            return gamma_best;
        }
    }

    return gamma_best;
}

inline double Movement::getDilationCorrectedBeltLength(double belt_length, double F_belt) const {
    const double elongation_factor = 1.0 + BELT_ELONGATION_COEFFICIENT * F_belt;
    return belt_length / elongation_factor;
}

Movement::Lengths Movement::getBeltLengths(double x, double y) {
    const double frameX = x + minSafeXOffset;
    const double frameY = y + minSafeY;

    double gamma = gamma_last_position;
    double phi_L = 0.0;
    double phi_R = 0.0;
    double F_L = 0.0;
    double F_R = 0.0;

    constexpr int solver_max_iterations = 20;
    constexpr double gamma_delta_termination = 0.25 / 180.0 * PI;

    for (int i = 0; i < solver_max_iterations; i++) {
        getBeltAngles(frameX, frameY, gamma, phi_L, phi_R);
        getBeltForces(phi_L, phi_R, F_L, F_R);

        const double gamma_last = gamma;
        gamma = solveTorqueEquilibrium(phi_L, phi_R, F_L, F_R, gamma);
        if (fabs(gamma_last - gamma) < gamma_delta_termination) {
            break;
        }
    }
    gamma_last_position = gamma;

    double leftX, leftY, rightX, rightY;
    getLeftTangentPoint(frameX, frameY, gamma, leftX, leftY);
    getRightTangentPoint(frameX, frameY, gamma, rightX, rightY);

    const double leftLegFlat = sqrt(leftX * leftX + leftY * leftY);
    const double rightLegFlat = sqrt((topDistance - rightX) * (topDistance - rightX) + rightY * rightY);

    double leftLeg = sqrt(leftLegFlat * leftLegFlat + MID_PULLEY_TO_WALL * MID_PULLEY_TO_WALL);
    double rightLeg = sqrt(rightLegFlat * rightLegFlat + MID_PULLEY_TO_WALL * MID_PULLEY_TO_WALL);

    leftLeg = getDilationCorrectedBeltLength(leftLeg, F_L);
    rightLeg = getDilationCorrectedBeltLength(rightLeg, F_R);

    const int32_t leftLegSteps = static_cast<int32_t>((leftLeg / circumference) * STEPS_PER_ROTATION);
    const int32_t rightLegSteps = static_cast<int32_t>((rightLeg / circumference) * STEPS_PER_ROTATION);

    return Lengths(leftLegSteps, rightLegSteps);
}

bool Movement::beginLinearTravel(double x, double y, int speed, float* outMoveTime, bool force) {
    if (topDistance == -1 || !homed) {
        return false;
    }
    if (x < 0.0 || (x - 1.0) > width || y < 0.0) {
        return false;
    }
    if (!force && !bridge->canEnqueue()) {
        return false;
    }

    Lengths lengths = getBeltLengths(x, y);
    int32_t deltaLeft = static_cast<int32_t>(labs(static_cast<long>(plannedLeftSteps - lengths.left)));
    int32_t deltaRight = static_cast<int32_t>(labs(static_cast<long>(plannedRightSteps - lengths.right)));

    if (deltaLeft == 0 && deltaRight == 0) {
        X = x;
        Y = y;
        if (outMoveTime) {
            *outMoveTime = 0.0f;
        }
        return true;
    }

    float spd = static_cast<float>(constrain(speed, MIN_DRAW_SPEED, MAX_DRAW_SPEED));
    float leftSpeed, rightSpeed, moveTime;
    if (deltaLeft >= deltaRight) {
        leftSpeed = spd;
        moveTime = static_cast<float>(deltaLeft) / leftSpeed;
        rightSpeed = static_cast<float>(deltaRight) / moveTime;
    } else {
        rightSpeed = spd;
        moveTime = static_cast<float>(deltaRight) / rightSpeed;
        leftSpeed = static_cast<float>(deltaLeft) / moveTime;
    }

    if (!bridge->queueMoveTo(lengths.left, lengths.right, leftSpeed, rightSpeed, force)) {
        return false;
    }

    plannedLeftSteps = lengths.left;
    plannedRightSteps = lengths.right;
    X = x;
    Y = y;
    if (outMoveTime) {
        *outMoveTime = moveTime;
    }
    return true;
}

void Movement::extendBelts(int distanceMm) {
    const int32_t steps = static_cast<int32_t>((static_cast<double>(distanceMm) / circumference) * STEPS_PER_ROTATION);
    bridge->extendRelative(steps, static_cast<float>(MOVE_SPEED_STEPS));
    plannedLeftSteps += steps;
    plannedRightSteps += steps;
}
