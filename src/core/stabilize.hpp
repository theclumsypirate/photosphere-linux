#pragma once

#include "core/rotation.hpp"
#include "io/insv_telemetry.hpp"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace ps {

enum class StabilizeMode { HorizonLock, FlowState };

struct StabilizeOptions {
    StabilizeMode mode = StabilizeMode::HorizonLock;
    double smoothSeconds = 0.8;
    double horizonStrength = 1.0;
    double yawStrength = 0.7;
    double pitchStrength = 0.85;
};

// Builds a rotation for each frame time from the camera's inertial log.
// The rotation is applied to an equirectangular panorama so that the content
// stays level and, in FlowState mode, camera shake is removed.
class Stabilizer {
public:
    bool load(const io::InsvTelemetry& telemetry,
              const StabilizeOptions& options, std::string& error);

    // Rotation to apply to the panorama at a time in seconds from the start
    // of the recording. Identity outside the logged interval.
    Mat3 rotationAt(double timeSeconds) const;

    // Integrated camera-to-world orientation at the same time. Used to keep
    // a first-order ambisonic sound field aligned with the picture.
    Mat3 orientationAt(double timeSeconds) const;

    cv::Mat apply(const cv::Mat& equirect, double timeSeconds) const;

    bool valid() const { return !times_.empty(); }
    double durationSeconds() const;
    double rollAt(double timeSeconds) const;

private:
    std::vector<double> times_;
    std::vector<Mat3> rotations_;
    std::vector<Mat3> orientations_;
};

// Samples an equirectangular image through a 3D rotation. Longitude wraps,
// latitude is clamped at the poles.
cv::Mat rotateEquirect(const cv::Mat& equirect, const Mat3& rotation);

} // namespace ps
