#pragma once

#include <opencv2/core.hpp>

#include <vector>

#include "core/lens_model.hpp"
#include "core/spherical.hpp"
#include "core/types.hpp"

namespace ps {

struct PreparedFrame {
    cv::Mat image;                 // CV_8UC3
    CameraBasis basis;
    FrameIntrinsics intrinsics;
    double gain = 1.0;
    int lens = 0;
};

struct RenderOptions {
    int width = 4096;
    double longitudeSpanDegrees = 360.0;
    double centerLongitudeDegrees = 0.0;
    double latitudeSpanDegrees = 180.0;
    double centerLatitudeDegrees = 0.0;
    double pivotRatio = 0.0;
    bool multiband = true;
};

Panorama renderPanorama(const std::vector<PreparedFrame>& frames,
                        const RenderOptions& options);

} // namespace ps
