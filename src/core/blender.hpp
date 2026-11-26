#pragma once

#include <opencv2/core.hpp>

#include <utility>
#include <vector>

namespace ps {

// Burt-Adelson Laplacian pyramid blending. The renderer accumulates one
// weighted colour sum and one weight sum per level; this class folds them back
// into a single image. Ported from MultibandBlender.kt.
struct PyramidLevel {
    cv::Mat color;  // CV_32FC3, weighted sum
    cv::Mat weight; // CV_32FC1
};

int levelCountFor(int canvasHeight);
std::pair<int, int> levelSize(int width, int height, int level);

cv::Mat normalizeColor(const cv::Mat& color, const cv::Mat& weight);

// levels[0] is finest, levels.back() is coarsest.
cv::Mat reconstructPyramid(const std::vector<PyramidLevel>& levels);

} // namespace ps
