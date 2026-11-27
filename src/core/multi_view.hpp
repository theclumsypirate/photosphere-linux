#pragma once

#include <opencv2/core.hpp>

namespace ps {

struct VirtualCamera {
    double yawDegrees = 0.0;
    double pitchDegrees = 0.0; // negative above horizon
    double fovDegrees = 75.0;
};

// Renders a perspective view out of an equirectangular panorama.
cv::Mat renderVirtualView(const cv::Mat& equirect, const VirtualCamera& camera,
                          int width, int height);

// Two virtual cameras in one frame: either a 50/50 split or a picture in
// picture with the second camera in the lower right corner.
cv::Mat renderSplitView(const cv::Mat& equirect, const VirtualCamera& a,
                        const VirtualCamera& b, int width, int height);
cv::Mat renderPipView(const cv::Mat& equirect, const VirtualCamera& main,
                      const VirtualCamera& inset, int width, int height,
                      double insetFraction = 0.35);

} // namespace ps
