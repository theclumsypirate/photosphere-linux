#include "core/multi_view.hpp"

#include "core/spherical.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace ps {
namespace {
constexpr double kPi = 3.14159265358979323846;
}

cv::Mat renderVirtualView(const cv::Mat& equirect, const VirtualCamera& camera,
                          int width, int height) {
    if (equirect.empty() || width <= 0 || height <= 0) return {};
    const CameraBasis basis = basisFromPose(
        CameraPose{camera.yawDegrees, camera.pitchDegrees, 0.0,
                   rotation::identity(), false});
    const double tanX = std::tan(camera.fovDegrees * kPi / 360.0);
    const double tanY = tanX * height / width;
    cv::Mat mapX(height, width, CV_32FC1);
    cv::Mat mapY(height, width, CV_32FC1);
    for (int row = 0; row < height; ++row) {
        const double cy = -(static_cast<double>(row) / height - 0.5) * 2.0 * tanY;
        auto* mx = mapX.ptr<float>(row);
        auto* my = mapY.ptr<float>(row);
        for (int col = 0; col < width; ++col) {
            const double cx = (static_cast<double>(col) / width - 0.5) * 2.0 * tanX;
            const Vec3 d = basis.toWorld(cx, cy, 1.0);
            const double lon = std::atan2(d[0], d[1]);
            const double lat = std::asin(std::clamp(d[2], -1.0, 1.0));
            mx[col] = static_cast<float>(equirect::columnFor(
                lon * 180.0 / kPi, equirect.cols));
            my[col] = static_cast<float>(equirect::rowFor(
                lat * 180.0 / kPi, equirect.rows));
        }
    }
    cv::Mat out;
    cv::remap(equirect, out, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_WRAP);
    return out;
}

cv::Mat renderSplitView(const cv::Mat& equirect, const VirtualCamera& a,
                        const VirtualCamera& b, int width, int height) {
    const cv::Mat left = renderVirtualView(equirect, a, width / 2, height);
    const cv::Mat right = renderVirtualView(equirect, b, width / 2, height);
    if (left.empty() || right.empty()) return {};
    cv::Mat out(height, width, left.type());
    left.copyTo(out(cv::Rect(0, 0, width / 2, height)));
    right.copyTo(out(cv::Rect(width / 2, 0, width - width / 2, height)));
    cv::line(out, cv::Point(width / 2, 0), cv::Point(width / 2, height - 1),
             cv::Scalar(30, 32, 36), 2);
    return out;
}

cv::Mat renderPipView(const cv::Mat& equirect, const VirtualCamera& main,
                      const VirtualCamera& inset, int width, int height,
                      double insetFraction) {
    cv::Mat out = renderVirtualView(equirect, main, width, height);
    if (out.empty()) return {};
    const int insetW = std::max(64, static_cast<int>(width * insetFraction));
    const int insetH = std::max(48, static_cast<int>(height * insetFraction));
    const cv::Mat small = renderVirtualView(equirect, inset, insetW, insetH);
    if (small.empty()) return out;
    const int x = width - insetW - 12;
    const int y = height - insetH - 12;
    small.copyTo(out(cv::Rect(x, y, insetW, insetH)));
    cv::rectangle(out, cv::Rect(x, y, insetW, insetH),
                  cv::Scalar(30, 32, 36), 3);
    return out;
}

} // namespace ps
