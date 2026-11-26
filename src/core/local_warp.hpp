#pragma once

#include <opencv2/core.hpp>

#include <vector>

namespace ps {

// Coarse APAP style correction grid. Residuals from matched features are
// binned into cells, smoothed, and applied to the ideal pixel before lens
// distortion. The correction is clamped to a small fraction of the frame.
struct LocalWarp {
    int cols = 0;
    int rows = 0;
    std::vector<cv::Point2f> correction;

    bool empty() const { return correction.empty(); }

    cv::Point2f at(double column, double row, double width, double height) const;
};

LocalWarp estimateLocalWarp(const std::vector<cv::Point2f>& pixels,
                            const std::vector<cv::Point2f>& residuals,
                            int width, int height);

} // namespace ps
