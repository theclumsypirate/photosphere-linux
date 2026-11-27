#pragma once

#include <opencv2/core.hpp>

namespace ps {

// A GoPro MAX EAC frame holds six equi-angular cube faces in a 3:1 rectangle.
bool isEacFrameSize(int width, int height);

// Reprojects a stacked EAC image (two video tracks joined vertically) into a
// 2:1 equirectangular panorama. outWidth <= 0 keeps the EAC width.
cv::Mat eacToEquirect(const cv::Mat& eac, int outWidth = 0);

} // namespace ps
