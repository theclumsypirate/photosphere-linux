#pragma once

#include <opencv2/core.hpp>

#include <functional>
#include <vector>

#include "core/lens_model.hpp"
#include "core/local_warp.hpp"
#include "core/seam.hpp"
#include "core/spherical.hpp"
#include "core/types.hpp"

namespace ps {

struct PreparedFrame {
    cv::Mat image;                 // CV_8UC3
    CameraBasis basis;
    FrameIntrinsics intrinsics;
    double gain = 1.0;
    double quality = 1.0; // 0..1, downweights frames with poor feature agreement
    const LocalWarp* warp = nullptr;
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
    std::function<void(int, int)> onProgress;
};

Panorama renderPanorama(const std::vector<PreparedFrame>& frames,
                        const RenderOptions& options,
                        const SeamWeights* seams = nullptr);

} // namespace ps
