#include "core/spherical.hpp"

#include <algorithm>
#include <cmath>

namespace ps {
namespace {

constexpr double kPi = 3.14159265358979323846;
inline double radians(double d) { return d * kPi / 180.0; }
inline double degrees(double r) { return r * 180.0 / kPi; }

} // namespace

CameraBasis basisFromPose(const CameraPose& pose) {
    if (pose.hasMatrix) return basisFromMatrix(pose.matrix);
    const double yaw = radians(pose.yawDegrees);
    const double elevation = radians(pose.elevationDegrees());
    const double roll = radians(pose.rollDegrees);
    const double sy = std::sin(yaw), cy = std::cos(yaw);
    const double ce = std::cos(elevation);

    const Vec3 forward{sy * ce, cy * ce, std::sin(elevation)};
    const Vec3 right0{cy, -sy, 0.0};
    const Vec3 up0{right0[1] * forward[2],
                   -right0[0] * forward[2],
                   right0[0] * forward[1] - right0[1] * forward[0]};
    const double cr = std::cos(roll), sr = std::sin(roll);
    CameraBasis b;
    b.forward = forward;
    b.right = {right0[0] * cr - up0[0] * sr,
               right0[1] * cr - up0[1] * sr,
               -up0[2] * sr};
    b.up = {up0[0] * cr + right0[0] * sr,
            up0[1] * cr + right0[1] * sr,
            up0[2] * cr};
    return b;
}

CameraBasis basisFromMatrix(const Mat3& m) {
    CameraBasis b;
    b.right = {m[0], m[3], m[6]};
    b.up = {-m[1], -m[4], -m[7]};
    b.forward = {m[2], m[5], m[8]};
    return b;
}

Mat3 basisToMatrix(const CameraBasis& b) {
    return {b.right[0], -b.up[0], b.forward[0],
            b.right[1], -b.up[1], b.forward[1],
            b.right[2], -b.up[2], b.forward[2]};
}

CameraPose basisToPose(const CameraBasis& b) {
    CameraPose p;
    p.yawDegrees = degrees(std::atan2(b.forward[0], b.forward[1]));
    const double elevation = degrees(std::asin(std::clamp(b.forward[2], -1.0, 1.0)));
    p.pitchDegrees = -elevation;
    const double yr = radians(p.yawDegrees), er = radians(elevation);
    const Vec3 up0{-std::sin(yr) * std::sin(er),
                   -std::cos(yr) * std::sin(er),
                   std::cos(er)};
    p.rollDegrees = degrees(std::atan2(-rotation::dot3(b.right, up0),
                                        rotation::dot3(b.up, up0)));
    return p;
}

namespace equirect {

Vec3 direction(double longitudeDeg, double latitudeDeg) {
    const double lon = radians(longitudeDeg), lat = radians(latitudeDeg);
    const double cl = std::cos(lat);
    return {std::sin(lon) * cl, std::cos(lon) * cl, std::sin(lat)};
}

double longitudeOf(const Vec3& d) { return degrees(std::atan2(d[0], d[1])); }
double latitudeOf(const Vec3& d) {
    return degrees(std::asin(std::clamp(d[2], -1.0, 1.0)));
}

} // namespace equirect

double featherWeight(double idealColumn, double idealRow, double centerX, double centerY) {
    const double across = 1.0 - std::abs(idealColumn - centerX) / centerX;
    if (across <= 0.0) return 0.0;
    const double down = 1.0 - std::abs(idealRow - centerY) / centerY;
    if (down <= 0.0) return 0.0;
    const double linear = across * down;
    return linear * linear * linear;
}

} // namespace ps
