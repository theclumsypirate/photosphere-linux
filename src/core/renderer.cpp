#include "core/renderer.hpp"

#include "core/blender.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ps {
namespace {

constexpr double kMinDepth = 1e-4;

void accumulateLevel(const std::vector<PreparedFrame>& frames,
                     cv::Mat& colorSum, cv::Mat& weightSum,
                     const RenderOptions& options, int level,
                     const SeamWeights* seams, double& coveredPixels) {
    const int divisor = 1 << level;
    const int w = colorSum.cols, h = colorSum.rows;
    const int interpolation = level == 0 ? cv::INTER_LANCZOS4 : cv::INTER_LINEAR;

    std::vector<double> forwardH(w), rightH(w), upH(w);
    for (size_t fi = 0; fi < frames.size(); ++fi) {
        const PreparedFrame& frame = frames[fi];
        if (frame.image.empty()) continue;
        cv::Mat source;
        if (divisor > 1) {
            cv::resize(frame.image, source,
                       cv::Size(std::max(1, frame.intrinsics.widthPx / divisor),
                                std::max(1, frame.intrinsics.heightPx / divisor)),
                       0.0, 0.0, cv::INTER_AREA);
        } else {
            source = frame.image;
        }
        if (source.empty()) continue;

        const CameraBasis& b = frame.basis;
        const FrameIntrinsics& in = frame.intrinsics;
        const double cx = in.centerXPx(), cy = in.centerYPx();
        const double maxColumn = in.widthPx - 1.0, maxRow = in.heightPx - 1.0;

        for (int col = 0; col < w; ++col) {
            const double lon = equirect::longitudeDegrees(
                col, w, options.longitudeSpanDegrees, options.centerLongitudeDegrees);
            const double r = lon * M_PI / 180.0;
            const double sl = std::sin(r), cl = std::cos(r);
            forwardH[col] = sl * b.forward[0] + cl * b.forward[1];
            rightH[col] = sl * b.right[0] + cl * b.right[1];
            upH[col] = sl * b.up[0] + cl * b.up[1];
        }

        cv::Mat mapX(h, w, CV_32FC1), mapY(h, w, CV_32FC1), weights(h, w, CV_32FC1);
        for (int row = 0; row < h; ++row) {
            const double lat = equirect::latitudeDegrees(
                row, h, options.latitudeSpanDegrees, options.centerLatitudeDegrees);
            const double r = lat * M_PI / 180.0;
            const double cl = std::cos(r), sl = std::sin(r);
            auto* mx = mapX.ptr<float>(row);
            auto* my = mapY.ptr<float>(row);
            auto* ww = weights.ptr<float>(row);
            for (int col = 0; col < w; ++col) {
                const double depth = cl * forwardH[col] + sl * b.forward[2] - options.pivotRatio;
                if (depth <= kMinDepth) { mx[col] = -1.f; my[col] = -1.f; ww[col] = 0.f; continue; }
                const double lateral = cl * rightH[col] + sl * b.right[2];
                const double vertical = cl * upH[col] + sl * b.up[2];
                double idealX = cx + in.focalXPx * lateral / depth;
                double idealY = cy - in.focalYPx * vertical / depth;
                if (frame.warp && !frame.warp->empty()) {
                    const cv::Point2f correction =
                        frame.warp->at(idealX, idealY, in.widthPx, in.heightPx);
                    idealX += correction.x;
                    idealY += correction.y;
                }
                double sx = idealX, sy = idealY;
                if (in.hasRadial) {
                    const double x = idealX - cx, y = idealY - cy;
                    const double factor = radialFactor(in.radial, x * x + y * y);
                    sx = cx + x * factor;
                    sy = cy + y * factor;
                }
                if (sx < 0.0 || sx > maxColumn || sy < 0.0 || sy > maxRow) {
                    mx[col] = -1.f; my[col] = -1.f; ww[col] = 0.f; continue;
                }
                mx[col] = static_cast<float>(sx / divisor);
                my[col] = static_cast<float>(sy / divisor);
                const float baseWeight = seams
                                             ? seams->weightFor(fi, row, col, divisor)
                                             : static_cast<float>(featherWeight(
                                                   idealX, idealY, cx, cy));
                ww[col] = baseWeight * static_cast<float>(frame.quality);
            }
        }
        if (level == 0) coveredPixels += cv::countNonZero(weights);

        cv::Mat warped;
        cv::remap(source, warped, mapX, mapY, interpolation, cv::BORDER_CONSTANT,
                  cv::Scalar(0, 0, 0));
        cv::Mat warpedFloat;
        warped.convertTo(warpedFloat, CV_32FC3);
        cv::Mat weight3;
        cv::merge(std::vector<cv::Mat>{weights, weights, weights}, weight3);
        cv::multiply(warpedFloat, weight3, warpedFloat);
        if (std::abs(frame.gain - 1.0) > 1e-6) {
            warpedFloat *= frame.gain;
        }
        cv::add(colorSum, warpedFloat, colorSum);
        cv::add(weightSum, weights, weightSum);
    }
}

} // namespace

Panorama renderPanorama(const std::vector<PreparedFrame>& frames,
                        const RenderOptions& options,
                        const SeamWeights* seams) {
    Panorama out;
    if (frames.empty() || options.width <= 0) return out;
    const int width = options.width;
    const int height = std::max(1, static_cast<int>(std::lround(
        width * options.latitudeSpanDegrees / options.longitudeSpanDegrees)));
    out.width = width;
    out.height = height;

    const int levels = options.multiband ? levelCountFor(height) : 1;
    std::vector<PyramidLevel> pyramid;
    pyramid.reserve(levels);
    double covered = 0.0;
    for (int level = 0; level < levels; ++level) {
        const auto [lw, lh] = levelSize(width, height, level);
        PyramidLevel pl;
        pl.color = cv::Mat::zeros(lh, lw, CV_32FC3);
        pl.weight = cv::Mat::zeros(lh, lw, CV_32FC1);
        accumulateLevel(frames, pl.color, pl.weight, options, level, seams, covered);
        pyramid.push_back(std::move(pl));
        if (options.onProgress) options.onProgress(level + 1, levels);
    }
    if (levels == 1) {
        out.equirect = normalizeColor(pyramid[0].color, pyramid[0].weight);
    } else {
        out.equirect = reconstructPyramid(pyramid);
    }
    out.seamMask = pyramid.front().weight.clone();
    return out;
}

} // namespace ps
