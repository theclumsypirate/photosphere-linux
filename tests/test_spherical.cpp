#include "core/spherical.hpp"

#include <cmath>
#include <iostream>

namespace ps {

namespace {
bool close(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }
}

int runSphericalTests() {
    int failures = 0;
    const CameraBasis north = basisFromPose(CameraPose{0, 0, 0, rotation::identity(), false});
    if (!close(north.forward[0], 0.0) || !close(north.forward[1], 1.0) ||
        !close(north.forward[2], 0.0)) ++failures;
    if (!close(north.right[0], 1.0) || !close(north.up[2], 1.0)) ++failures;

    const Vec3 d = equirect::direction(90.0, 0.0);
    if (!close(d[0], 1.0) || !close(d[1], 0.0)) ++failures;
    if (!close(equirect::longitudeOf(d), 90.0) ||
        !close(equirect::latitudeOf(d), 0.0)) ++failures;

    const double col = equirect::columnFor(90.0, 4096);
    const double lon = equirect::longitudeDegrees(static_cast<int>(col + 0.5), 4096);
    if (std::abs(lon - 90.0) > 0.2) ++failures;

    const CameraPose p = basisToPose(basisFromPose(CameraPose{37, -12, 5, rotation::identity(), false}));
    if (std::abs(p.yawDegrees - 37.0) > 1e-4 ||
        std::abs(p.pitchDegrees + 12.0) > 1e-4 ||
        std::abs(p.rollDegrees - 5.0) > 1e-4) ++failures;

    if (!close(featherWeight(10, 10, 10, 10), 1.0)) ++failures;
    if (!close(featherWeight(20, 10, 10, 10), 0.0)) ++failures;
    if (!close(featherWeight(-2, 10, 10, 10), 0.0)) ++failures;
    return failures;
}

} // namespace ps
