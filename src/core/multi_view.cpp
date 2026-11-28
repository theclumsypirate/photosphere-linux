#include "core/multi_view.hpp"

#include "core/spherical.hpp"

#include <opencv2/core/utility.hpp>
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
    // The main viewer uses a vertical field of view, like QMatrix4x4::perspective.
    const double tanY = std::tan(camera.fovDegrees * kPi / 360.0);
    const double tanX = tanY * width / height;
    const double rx = basis.right[0], ry = basis.right[1], rz = basis.right[2];
    const double ux = basis.up[0], uy = basis.up[1], uz = basis.up[2];
    const double fx = basis.forward[0], fy = basis.forward[1], fz = basis.forward[2];
    const double xStep = 2.0 * tanX / width;
    const double xStart = -tanX + xStep * 0.5;
    const double yStep = 2.0 * tanY / height;
    const double yStart = tanY - yStep * 0.5;
    const double lonScale = equirect.cols / 360.0;
    const double latScale = equirect.rows / 180.0;
    const float xOffset = static_cast<float>(equirect.cols * 0.5 - 0.5);
    const float yOffset = static_cast<float>(equirect.rows * 0.5 - 0.5);
    cv::Mat mapX(height, width, CV_32FC1);
    cv::Mat mapY(height, width, CV_32FC1);
    cv::parallel_for_(cv::Range(0, height), [&](const cv::Range& range) {
        for (int row = range.start; row < range.end; ++row) {
            const double cy = yStart - yStep * row;
            auto* mx = mapX.ptr<float>(row);
            auto* my = mapY.ptr<float>(row);
            for (int col = 0; col < width; ++col) {
                const double cx = xStart + xStep * col;
                double dx = cx * rx + cy * ux + fx;
                double dy = cx * ry + cy * uy + fy;
                double dz = cx * rz + cy * uz + fz;
                const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
                dx /= length;
                dy /= length;
                dz /= length;
                const double lon = std::atan2(dx, dy) * 180.0 / kPi;
                const double lat =
                    std::asin(std::clamp(dz, -1.0, 1.0)) * 180.0 / kPi;
                mx[col] = static_cast<float>(lon * lonScale) + xOffset;
                my[col] = yOffset - static_cast<float>(lat * latScale);
            }
        }
    });
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

cv::Mat renderSplitView(const cv::Mat& first, const VirtualCamera& a,
                        const cv::Mat& second, const VirtualCamera& b,
                        int width, int height) {
    const cv::Mat left = renderVirtualView(first, a, width / 2, height);
    const cv::Mat right = renderVirtualView(second, b, width / 2, height);
    if (left.empty() || right.empty()) return {};
    cv::Mat out(height, width, left.type());
    left.copyTo(out(cv::Rect(0, 0, width / 2, height)));
    right.copyTo(out(cv::Rect(width / 2, 0, width - width / 2, height)));
    cv::line(out, cv::Point(width / 2, 0), cv::Point(width / 2, height - 1),
             cv::Scalar(30, 32, 36), 2);
    return out;
}

cv::Mat renderPipView(const cv::Mat& first, const VirtualCamera& main,
                      const cv::Mat& second, const VirtualCamera& inset,
                      int width, int height, double insetFraction) {
    cv::Mat out = renderVirtualView(first, main, width, height);
    if (out.empty()) return {};
    const int insetW = std::max(96, static_cast<int>(width * insetFraction));
    const int insetH = std::max(64, static_cast<int>(height * insetFraction));
    const cv::Mat small = renderVirtualView(second, inset, insetW, insetH);
    if (small.empty()) return out;
    const int x = width - insetW - 16;
    const int y = height - insetH - 16;
    small.copyTo(out(cv::Rect(x, y, insetW, insetH)));
    cv::rectangle(out, cv::Rect(x, y, insetW, insetH),
                  cv::Scalar(30, 32, 36), 3);
    return out;
}

} // namespace ps
