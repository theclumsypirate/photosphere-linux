#include "core/stitcher.hpp"

#include "core/blender.hpp"
#include "core/renderer.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace ps {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct LensBasis {
    Vec3 forward;
    Vec3 right;
    Vec3 up;
};

// The two back-to-back fisheye lenses of an Insta360 style camera.
LensBasis lensBasis(int lens) {
    if (lens == 0) return {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}};
    return {{0, -1, 0}, {-1, 0, 0}, {0, 0, 1}};
}

void accumulateFisheyeLevel(const cv::Mat& image, int lens, double fovDegrees,
                            bool flipLenses, cv::Mat& colorSum, cv::Mat& weightSum,
                            int divisor) {
    if (image.empty()) return;
    cv::Mat source;
    if (divisor > 1) {
        cv::resize(image, source,
                   cv::Size(std::max(1, image.cols / divisor),
                            std::max(1, image.rows / divisor)),
                   0.0, 0.0, cv::INTER_AREA);
    } else {
        source = image;
    }
    const int w = colorSum.cols, h = colorSum.rows;
    const double cx = image.cols / 2.0 / divisor;
    const double cy = image.rows / 2.0 / divisor;
    const double radius = std::min(cx, cy);
    const double thetaMax = fovDegrees * kPi / 360.0;
    LensBasis basis = lensBasis(lens);
    if (flipLenses) {
        basis.right = rotation::scale3(basis.right, -1.0);
        basis.up = rotation::scale3(basis.up, -1.0);
    }

    cv::Mat mapX(h, w, CV_32FC1, cv::Scalar(-1.f));
    cv::Mat mapY(h, w, CV_32FC1, cv::Scalar(-1.f));
    cv::Mat weights(h, w, CV_32FC1, cv::Scalar(0.f));
    std::vector<double> dX(w), dY(w), dZ(w);
    for (int col = 0; col < w; ++col) {
        const double lon = equirect::longitudeDegrees(col, w) * kPi / 180.0;
        dX[col] = std::sin(lon);
        dY[col] = std::cos(lon);
    }
    for (int row = 0; row < h; ++row) {
        const double lat = equirect::latitudeDegrees(row, h) * kPi / 180.0;
        const double cl = std::cos(lat), sl = std::sin(lat);
        auto* mx = mapX.ptr<float>(row);
        auto* my = mapY.ptr<float>(row);
        auto* ww = weights.ptr<float>(row);
        for (int col = 0; col < w; ++col) {
            const Vec3 d{cl * dX[col], cl * dY[col], sl};
            const double cosTheta = std::clamp(rotation::dot3(d, basis.forward), -1.0, 1.0);
            const double theta = std::acos(cosTheta);
            if (theta > thetaMax) continue;
            const double ax = rotation::dot3(d, basis.right);
            const double ay = rotation::dot3(d, basis.up);
            const double phi = std::atan2(ax, ay);
            const double r = radius * theta / thetaMax;
            const double sx = cx + r * std::sin(phi);
            const double sy = cy - r * std::cos(phi);
            if (sx < 0 || sy < 0 || sx >= source.cols || sy >= source.rows) continue;
            mx[col] = static_cast<float>(sx);
            my[col] = static_cast<float>(sy);
            ww[col] = static_cast<float>(featherWeight(sx, sy, cx, cy));
        }
    }
    if (cv::countNonZero(weights) == 0) return;
    cv::Mat warped;
    cv::remap(source, warped, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_CONSTANT,
              cv::Scalar(0, 0, 0));
    cv::Mat warpedFloat;
    warped.convertTo(warpedFloat, CV_32FC3);
    cv::Mat weight3;
    cv::merge(std::vector<cv::Mat>{weights, weights, weights}, weight3);
    cv::multiply(warpedFloat, weight3, warpedFloat);
    cv::add(colorSum, warpedFloat, colorSum);
    cv::add(weightSum, weights, weightSum);
}

} // namespace

Panorama stitchDualFisheye(const std::vector<LensFrame>& pair,
                           const StitchOptions& options,
                           std::string& diagnostics) {
    Panorama out;
    if (pair.size() < 2) { diagnostics = "need two fisheye lenses"; return out; }
    const cv::Mat* images[2] = {&pair[0].image, &pair[1].image};
    if (images[0]->empty() || images[1]->empty()) {
        diagnostics = "empty fisheye image";
        return out;
    }
    const int w = options.width;
    const int h = std::max(2, w / 2);
    out.width = w;
    out.height = h;
    const int levels = options.multiband ? levelCountFor(h) : 1;
    std::vector<PyramidLevel> pyramid;
    pyramid.reserve(levels);
    for (int level = 0; level < levels; ++level) {
        const auto [lw, lh] = levelSize(w, h, level);
        PyramidLevel pl;
        pl.color = cv::Mat::zeros(lh, lw, CV_32FC3);
        pl.weight = cv::Mat::zeros(lh, lw, CV_32FC1);
        const int divisor = 1 << level;
        accumulateFisheyeLevel(*images[0], 0, options.fisheyeFovDegrees,
                               options.flipLenses, pl.color, pl.weight, divisor);
        accumulateFisheyeLevel(*images[1], 1, options.fisheyeFovDegrees,
                               options.flipLenses, pl.color, pl.weight, divisor);
        pyramid.push_back(std::move(pl));
    }
    out.equirect = levels == 1 ? normalizeColor(pyramid[0].color, pyramid[0].weight)
                               : reconstructPyramid(pyramid);
    out.seamMask = pyramid.front().weight.clone();
    const int covered = cv::countNonZero(out.seamMask);
    diagnostics = "dual fisheye, fov=" + std::to_string(options.fisheyeFovDegrees) +
                  ", coverage=" +
                  std::to_string(100.0 * covered / static_cast<double>(w * h)) + "%";
    return out;
}

Panorama stitchSession(const io::InputSession& session,
                       const StitchOptions& options,
                       std::string& diagnostics) {
    if (session.frames.empty()) { diagnostics = "no frames"; return {}; }
    std::vector<LensFrame> pair;
    for (const auto& f : session.frames) {
        if (f.lens == 0 || f.lens == 1) pair.push_back(f);
        if (pair.size() == 2 && pair[0].lens != pair[1].lens) break;
    }
    if (pair.size() >= 2 && pair[0].lens != pair[1].lens)
        return stitchDualFisheye(pair, options, diagnostics);

    std::vector<PreparedFrame> frames;
    const size_t count = options.maxFrames > 0
                             ? std::min(session.frames.size(),
                                        static_cast<size_t>(options.maxFrames))
                             : session.frames.size();
    for (size_t i = 0; i < count; ++i) {
        const auto& f = session.frames[i];
        PreparedFrame pf;
        pf.image = f.image;
        pf.intrinsics = FrameIntrinsics::fromFieldOfView(
            f.image.cols, f.image.rows, 75.0, 60.0);
        CameraPose pose;
        pose.yawDegrees = 360.0 * i / count;
        pf.basis = basisFromPose(pose);
        frames.push_back(std::move(pf));
    }
    RenderOptions ro;
    ro.width = options.width;
    ro.multiband = options.multiband;
    Panorama out = renderPanorama(frames, ro);
    diagnostics = "generic ring, " + std::to_string(frames.size()) + " frames";
    return out;
}

} // namespace ps
