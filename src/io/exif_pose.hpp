#pragma once

#include <array>
#include <string>

namespace ps::io {

struct ExifPose {
    bool valid = false;
    int index = -1;
    double yawDegrees = 0.0;
    double pitchDegrees = 0.0;
    double rollDegrees = 0.0;
    double horizontalFovDegrees = 0.0;
    double verticalFovDegrees = 0.0;
    bool hasMatrix = false;
    std::array<double, 9> matrix{}; // row major [right, -up, forward]
};

// Reads the capture pose stamped into the EXIF UserComment tag by the
// original capture application. Returns false when the tag is missing.
bool readExifPose(const std::string& path, ExifPose& out);

} // namespace ps::io
