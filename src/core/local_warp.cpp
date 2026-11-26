#include "core/local_warp.hpp"

#include <algorithm>
#include <cmath>

namespace ps {
namespace {
constexpr int kCols = 6;
constexpr int kRows = 6;
} // namespace

cv::Point2f LocalWarp::at(double column, double row, double width, double height) const {
    if (empty() || cols <= 0 || rows <= 0) return {};
    const double u = std::clamp(column / width, 0.0, 1.0) * (cols - 1);
    const double v = std::clamp(row / height, 0.0, 1.0) * (rows - 1);
    const int x0 = static_cast<int>(u), y0 = static_cast<int>(v);
    const int x1 = std::min(cols - 1, x0 + 1);
    const int y1 = std::min(rows - 1, y0 + 1);
    const double fx = u - x0, fy = v - y0;
    auto sample = [&](int x, int y) {
        return correction[static_cast<size_t>(y) * cols + x];
    };
    const cv::Point2f top = sample(x0, y0) * static_cast<float>(1.0 - fx) +
                            sample(x1, y0) * static_cast<float>(fx);
    const cv::Point2f bottom = sample(x0, y1) * static_cast<float>(1.0 - fx) +
                               sample(x1, y1) * static_cast<float>(fx);
    return top * static_cast<float>(1.0 - fy) + bottom * static_cast<float>(fy);
}

LocalWarp estimateLocalWarp(const std::vector<cv::Point2f>& pixels,
                            const std::vector<cv::Point2f>& residuals,
                            int width, int height) {
    LocalWarp warp;
    if (pixels.empty() || pixels.size() != residuals.size() || width <= 0 || height <= 0)
        return warp;
    warp.cols = kCols;
    warp.rows = kRows;
    std::vector<cv::Point2f> sum(static_cast<size_t>(kCols) * kRows);
    std::vector<int> count(static_cast<size_t>(kCols) * kRows, 0);
    for (size_t i = 0; i < pixels.size(); ++i) {
        const int x = std::clamp(static_cast<int>(pixels[i].x / width * kCols), 0, kCols - 1);
        const int y = std::clamp(static_cast<int>(pixels[i].y / height * kRows), 0, kRows - 1);
        const size_t index = static_cast<size_t>(y) * kCols + x;
        sum[index] += residuals[i];
        ++count[index];
    }
    const double limit = 0.02 * std::max(width, height);
    warp.correction.resize(static_cast<size_t>(kCols) * kRows);
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kCols; ++x) {
            const size_t index = static_cast<size_t>(y) * kCols + x;
            const float divisor = static_cast<float>(std::max(1, count[index]));
            cv::Point2f value = sum[index] / divisor;
            value.x = std::clamp(value.x, static_cast<float>(-limit), static_cast<float>(limit));
            value.y = std::clamp(value.y, static_cast<float>(-limit), static_cast<float>(limit));
            warp.correction[index] = value;
        }
    }
    // Two smoothing passes over the small grid.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<cv::Point2f> smoothed = warp.correction;
        for (int y = 0; y < kRows; ++y) {
            for (int x = 0; x < kCols; ++x) {
                cv::Point2f acc;
                int n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= kCols || ny >= kRows) continue;
                        acc += warp.correction[static_cast<size_t>(ny) * kCols + nx];
                        ++n;
                    }
                smoothed[static_cast<size_t>(y) * kCols + x] = acc / static_cast<float>(n);
            }
        }
        warp.correction = smoothed;
    }
    return warp;
}

} // namespace ps
