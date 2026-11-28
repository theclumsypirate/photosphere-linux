#pragma once

#include <string>
#include <vector>

namespace ps::io {

// One inertial measurement. Vectors are in the frame of lens 0 after the
// camera's own IMU-to-lens rotation has been applied.
struct ImuSample {
    double timestampUs = 0.0; // camera clock, microseconds
    double gyro[3] = {0.0, 0.0, 0.0}; // rad/s
    double acc[3] = {0.0, 0.0, 0.0}; // g
};

struct InsvTelemetry {
    std::string cameraModel;
    double fps = 0.0;
    double rollingShutterMs = 0.0;
    double gyroTimestampMs = 0.0;
    double firstFrameTimestampUs = 0.0;
    // Video time at which the first IMU sample was recorded. Video timestamps
    // start at zero, while the camera clock does not.
    double videoOffsetSeconds = 0.0;
    double lensYawDeg = 0.0;
    double lensPitchDeg = 0.0;
    double lensRollDeg = 0.0;
    bool hasLensPose = false;
    bool raw = false;
    double gyroRangeDps = 2000.0;
    double accRangeG = 16.0;
    std::vector<ImuSample> samples;

    // Seconds since the first sample, on the camera clock.
    double secondsAt(size_t index) const {
        if (samples.empty() || index >= samples.size()) return 0.0;
        return (samples[index].timestampUs - samples.front().timestampUs) / 1e6;
    }
};

// Reads the 'inst' trailer at the end of an INSV/MP4/MOV file. No video frames
// are decoded, so this is fast even for very large files.
bool readInsvTelemetry(const std::string& path, InsvTelemetry& out,
                       std::string& error);

} // namespace ps::io
