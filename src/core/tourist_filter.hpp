#pragma once

#include <functional>
#include <string>

namespace ps::core {

enum class TouristMethod {
    Cut,
    CutAndMedian,
    Median,
    NoiseAndMedian,
    InpaintAndMedian
};

struct TouristOptions {
    TouristMethod method = TouristMethod::Median;
    // People boxes are enlarged before masking, as in the original algorithm.
    float upscaleX = 1.05f;
    float upscaleY = 1.1f;
};

// Removes people from an aligned stack of JPG photos using OpenCV's built-in
// HOG people detector. No external model file is needed. The result is written
// next to the input folder. Progress is (done, total, message).
bool removeTourists(
    const std::string& folder, const TouristOptions& options,
    std::string& outputPath, std::string& error,
    const std::function<void(int, int, const std::string&)>& progress = {});

} // namespace ps::core
