#include "core/blender.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace ps {
namespace {
constexpr double kEpsilon = 1e-6;
}

int levelCountFor(int canvasHeight) {
    if (canvasHeight <= 0) return 1;
    int height = canvasHeight;
    int levels = 1;
    while (height > 8 && levels < 6) {
        height /= 2;
        ++levels;
    }
    return levels;
}

std::pair<int, int> levelSize(int width, int height, int level) {
    const int shift = std::max(0, level);
    return {std::max(1, width >> shift), std::max(1, height >> shift)};
}

cv::Mat normalizeColor(const cv::Mat& color, const cv::Mat& weight) {
    cv::Mat weightEps, weight3, out;
    cv::add(weight, cv::Scalar(kEpsilon), weightEps);
    cv::merge(std::vector<cv::Mat>{weightEps, weightEps, weightEps}, weight3);
    cv::divide(color, weight3, out);
    return out;
}

cv::Mat reconstructPyramid(const std::vector<PyramidLevel>& levels, bool asFloat) {
    if (levels.empty()) return {};
    cv::Mat result = normalizeColor(levels.back().color, levels.back().weight);
    for (int level = static_cast<int>(levels.size()) - 2; level >= 0; --level) {
        const PyramidLevel& fine = levels[level];
        const PyramidLevel& coarse = levels[level + 1];
        const cv::Size size(fine.color.cols, fine.color.rows);
        cv::Mat pUp, cUp, wUp;
        cv::resize(result, pUp, size, 0.0, 0.0, cv::INTER_LINEAR);
        cv::resize(coarse.color, cUp, size, 0.0, 0.0, cv::INTER_LINEAR);
        cv::resize(coarse.weight, wUp, size, 0.0, 0.0, cv::INTER_LINEAR);

        cv::Mat ratio;
        cv::add(wUp, cv::Scalar(kEpsilon), ratio);
        cv::divide(fine.weight, ratio, ratio);
        cv::Mat ratio3;
        cv::merge(std::vector<cv::Mat>{ratio, ratio, ratio}, ratio3);
        cv::Mat scaledUp;
        cv::multiply(cUp, ratio3, scaledUp);

        cv::Mat band;
        cv::subtract(fine.color, scaledUp, band);
        cv::Mat weightEps, weight3, normalized;
        cv::add(fine.weight, cv::Scalar(kEpsilon), weightEps);
        cv::merge(std::vector<cv::Mat>{weightEps, weightEps, weightEps}, weight3);
        cv::divide(band, weight3, normalized);
        cv::add(pUp, normalized, result);
    }
    if (asFloat) return result;
    cv::Mat out;
    result.convertTo(out, CV_8UC3);
    return out;
}

} // namespace ps
