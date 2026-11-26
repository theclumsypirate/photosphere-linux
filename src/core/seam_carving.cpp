#include "core/seam_carving.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/stitching/detail/seam_finders.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ps {
namespace {

constexpr double kPi = 3.14159265358979323846;

void warpFrame(const PreparedFrame& frame, int width, int height, int scale,
               cv::Mat& image, cv::Mat& mask) {
    image = cv::Mat::zeros(height, width, CV_8UC3);
    mask = cv::Mat::zeros(height, width, CV_8UC1);
    cv::Mat mapX(height, width, CV_32FC1, cv::Scalar(-1.f));
    cv::Mat mapY(height, width, CV_32FC1, cv::Scalar(-1.f));
    const CameraBasis& b = frame.basis;
    const FrameIntrinsics& in = frame.intrinsics;
    const double cx = in.centerXPx(), cy = in.centerYPx();
    const double maxCol = in.widthPx - 1.0, maxRow = in.heightPx - 1.0;
    std::vector<double> fh(width), rh(width), uh(width);
    for (int col = 0; col < width; ++col) {
        const double lon = equirect::longitudeDegrees(col, width) * kPi / 180.0;
        const double s = std::sin(lon), c = std::cos(lon);
        fh[col] = s * b.forward[0] + c * b.forward[1];
        rh[col] = s * b.right[0] + c * b.right[1];
        uh[col] = s * b.up[0] + c * b.up[1];
    }
    for (int row = 0; row < height; ++row) {
        const double lat = equirect::latitudeDegrees(row, height) * kPi / 180.0;
        const double cl = std::cos(lat), sl = std::sin(lat);
        auto* mx = mapX.ptr<float>(row);
        auto* my = mapY.ptr<float>(row);
        auto* mk = mask.ptr<uint8_t>(row);
        for (int col = 0; col < width; ++col) {
            const double depth = cl * fh[col] + sl * b.forward[2];
            if (depth <= 1e-4) continue;
            const double lateral = cl * rh[col] + sl * b.right[2];
            const double vertical = cl * uh[col] + sl * b.up[2];
            double x = cx + in.focalXPx * lateral / depth;
            double y = cy - in.focalYPx * vertical / depth;
            if (in.hasRadial) {
                const double dx = x - cx, dy = y - cy;
                const double factor = radialFactor(in.radial, dx * dx + dy * dy);
                x = cx + dx * factor;
                y = cy + dy * factor;
            }
            if (x < 0 || y < 0 || x > maxCol || y > maxRow) continue;
            mx[col] = static_cast<float>(x / scale);
            my[col] = static_cast<float>(y / scale);
            mk[col] = 255;
        }
    }
    cv::Mat source;
    cv::resize(frame.image, source,
               cv::Size(std::max(1, frame.intrinsics.widthPx / scale),
                        std::max(1, frame.intrinsics.heightPx / scale)),
               0.0, 0.0, cv::INTER_AREA);
    cv::remap(source, image, mapX, mapY, cv::INTER_LINEAR,
              cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
}

} // namespace

SeamWeights carveSeams(const std::vector<PreparedFrame>& frames,
                       int canvasWidth, int canvasHeight, int scale) {
    if (frames.empty() || scale < 1) return {};
    const int width = std::max(64, canvasWidth / scale);
    const int height = std::max(32, canvasHeight / scale);
    std::vector<cv::Mat> images, masks;
    images.reserve(frames.size());
    masks.reserve(frames.size());
    for (const auto& frame : frames) {
        cv::Mat image, mask;
        warpFrame(frame, width, height, scale, image, mask);
        images.push_back(image);
        masks.push_back(mask);
    }
    std::vector<cv::UMat> uImages, uMasks;
    for (auto& image : images) uImages.push_back(image.getUMat(cv::ACCESS_READ));
    for (auto& mask : masks) uMasks.push_back(mask.getUMat(cv::ACCESS_RW));
    std::vector<cv::Point> corners(frames.size(), cv::Point(0, 0));
    try {
        cv::detail::GraphCutSeamFinder finder(cv::detail::GraphCutSeamFinder::COST_COLOR);
        finder.find(uImages, corners, uMasks);
        for (size_t i = 0; i < masks.size(); ++i)
            masks[i] = uMasks[i].getMat(cv::ACCESS_READ);
    } catch (const cv::Exception&) {
        // The graph cut backend can reject unusual mask shapes. Fall back to
        // nearest optical centre, which always covers the whole canvas.
        for (auto& mask : masks) mask.setTo(cv::Scalar(0));
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                const Vec3 dir = equirect::direction(
                    equirect::longitudeDegrees(col, width),
                    equirect::latitudeDegrees(row, height));
                double best = 0.0;
                int winner = -1;
                for (size_t i = 0; i < frames.size(); ++i) {
                    const double depth = rotation::dot3(dir, frames[i].basis.forward);
                    if (depth > best) { best = depth; winner = static_cast<int>(i); }
                }
                if (winner >= 0)
                    masks[static_cast<size_t>(winner)].ptr<uint8_t>(row)[col] = 255;
            }
        }
    }

    const size_t cells = static_cast<size_t>(width) * height;
    std::vector<int> labels(cells, -1);
    for (size_t i = 0; i < masks.size(); ++i) {
        for (int row = 0; row < height; ++row) {
            const auto* m = masks[i].ptr<uint8_t>(row);
            for (int col = 0; col < width; ++col)
                if (m[col]) labels[static_cast<size_t>(row) * width + col] = static_cast<int>(i);
        }
    }
    const auto [loserLabel, loserWeight] =
        deriveSeamFeather(labels, width, height, 3, true);
    SeamWeights out;
    out.gridWidth = width;
    out.gridHeight = height;
    out.scale = scale;
    out.labelMap = labels;
    out.loserLabel = loserLabel;
    out.loserWeight = loserWeight;
    return out;
}

} // namespace ps
