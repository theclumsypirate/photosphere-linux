#pragma once

#include <opencv2/core.hpp>

namespace ps {

struct LittlePlanetOptions {
    int outputSize = 2048;
    double fovDegrees = 180.0;
    double tiltDegrees = 0.0;
    double panDegrees = 0.0;
    double spinDegrees = 0.0;
};

// Stereographic projection from the nadir, ported from LittlePlanetConverter.kt.
// Returns a square BGRA image with transparent corners.
cv::Mat convertToLittlePlanet(const cv::Mat& equirect, const LittlePlanetOptions& options);

} // namespace ps
