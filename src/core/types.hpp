#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace ps {

// One lens image. A DNG frame contributes two of these, a side by side JPG
// contributes two, and an INSV frame contributes two per decoded timestamp.
struct LensFrame {
    cv::Mat image;          // 8 bit BGR
    std::string source;     // original file path
    int lens = 0;           // 0 or 1
    int sequence = 0;       // capture order
    double timestamp = 0.0; // seconds, when available
};

struct CameraModel {
    double focal = 0.0;     // in pixels, after crop
    cv::Point2d center{0.0, 0.0};
    double fovDeg = 195.0;  // circular fisheye field of view
    bool calibrated = false;
};

struct Panorama {
    cv::Mat equirect;       // 2:1 equirectangular result
    cv::Mat seamMask;
    int width = 0;
    int height = 0;
};

struct LoadResult {
    std::vector<LensFrame> frames;
    std::vector<CameraModel> models;
    std::string error;
    bool ok() const { return error.empty(); }
};

} // namespace ps
