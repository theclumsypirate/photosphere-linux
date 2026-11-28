#pragma once

#include <opencv2/core.hpp>

namespace ps {

// CPU equivalent of the GPU fisheye viewer shader. The input is one
// side-by-side frame; the output is the stitched perspective camera view.
cv::Mat renderFisheyeView(const cv::Mat& sideBySide,
                          double yawDegrees, double pitchDegrees,
                          double fovDegrees, double lensFovDegrees,
                          int width, int height);

} // namespace ps
