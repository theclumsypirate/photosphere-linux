#pragma once

#include "core/rotation.hpp"

namespace ps {

struct CameraPose {
    double yawDegrees = 0.0;
    double pitchDegrees = 0.0;   // negative above the horizon
    double rollDegrees = 0.0;
    Mat3 matrix = rotation::identity();
    bool hasMatrix = false;

    double elevationDegrees() const { return -pitchDegrees; }
};

// Orthonormal camera axes in the world frame (X east, Y north, Z up).
// The camera triple is left handed: up = right x forward.
struct CameraBasis {
    Vec3 forward{0, 0, 1};
    Vec3 right{1, 0, 0};
    Vec3 up{0, 1, 0};

    double depthOf(const Vec3& d) const { return rotation::dot3(d, forward); }
    double lateralOf(const Vec3& d) const { return rotation::dot3(d, right); }
    double verticalOf(const Vec3& d) const { return rotation::dot3(d, up); }
    Vec3 toWorld(double cameraX, double cameraY, double cameraZ) const {
        return {cameraX * right[0] + cameraY * up[0] + cameraZ * forward[0],
                cameraX * right[1] + cameraY * up[1] + cameraZ * forward[1],
                cameraX * right[2] + cameraY * up[2] + cameraZ * forward[2]};
    }
};

CameraBasis basisFromPose(const CameraPose& pose);
CameraBasis basisFromMatrix(const Mat3& matrix);

// [right, -up, forward] columns, a proper rotation for the algebra downstream.
Mat3 basisToMatrix(const CameraBasis& basis);
CameraPose basisToPose(const CameraBasis& basis);

namespace equirect {

constexpr double DEFAULT_LONGITUDE_SPAN = 360.0;
constexpr double DEFAULT_LATITUDE_SPAN = 180.0;

inline double longitudeDegrees(int col, int width, double span = DEFAULT_LONGITUDE_SPAN,
                               double center = 0.0) {
    return center + (col + 0.5) / width * span - span / 2.0;
}
inline double latitudeDegrees(int row, int height, double span = DEFAULT_LATITUDE_SPAN,
                              double center = 0.0) {
    return center + span / 2.0 - (row + 0.5) / height * span;
}
inline double columnFor(double longitude, int width, double span = DEFAULT_LONGITUDE_SPAN,
                        double center = 0.0) {
    return (longitude - center + span / 2.0) / span * width - 0.5;
}
inline double rowFor(double latitude, int height, double span = DEFAULT_LATITUDE_SPAN,
                     double center = 0.0) {
    return (center + span / 2.0 - latitude) / span * height - 0.5;
}

// Unit world direction at a longitude and latitude, X east / Y north / Z up.
Vec3 direction(double longitudeDeg, double latitudeDeg);
double longitudeOf(const Vec3& d);
double latitudeOf(const Vec3& d);

} // namespace equirect

// Cubed feather measured against the ideal pinhole pixel.
double featherWeight(double idealColumn, double idealRow, double centerX, double centerY);

} // namespace ps
