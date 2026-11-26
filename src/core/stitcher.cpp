#include "core/stitcher.hpp"

#include "core/blender.hpp"
#include "core/exposure.hpp"
#include "core/local_warp.hpp"
#include "core/pose_refiner.hpp"
#include "core/seam.hpp"
#include "core/seam_carving.hpp"
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
    diagnostics += "dual fisheye, fov=" + std::to_string(options.fisheyeFovDegrees) +
                  ", coverage=" +
                  std::to_string(100.0 * covered / static_cast<double>(w * h)) + "%";
    return out;
}

namespace {

Panorama stitchMultiFrame(const std::vector<LensFrame>& frames,
                          const StitchOptions& options,
                          std::string& diagnostics) {
    std::vector<LensFrame> ordered;
    for (const auto& f : frames)
        if (f.hasPose) ordered.push_back(f);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const LensFrame& a, const LensFrame& b) {
                         if (a.captureIndex >= 0 && b.captureIndex >= 0)
                             return a.captureIndex < b.captureIndex;
                         return a.sequence < b.sequence;
                     });
    std::vector<PoseFrame> poseFrames;
    poseFrames.reserve(ordered.size());
    for (const auto& f : ordered) {
        PoseFrame pf;
        pf.image = f.image;
        const bool portrait = f.image.rows > f.image.cols;
        const double hfov = f.horizontalFovDegrees > 0 ? f.horizontalFovDegrees
                                                       : (portrait ? 65.0 : 80.0);
        const double vfov = f.verticalFovDegrees > 0 ? f.verticalFovDegrees
                                                     : (portrait ? 80.0 : 65.0);
        pf.intrinsics = FrameIntrinsics::fromFieldOfView(
            f.image.cols, f.image.rows, hfov, vfov);
        CameraPose pose;
        pose.yawDegrees = f.yawDegrees;
        // EXIF pitch follows the capture convention: negative is above horizon.
        pose.pitchDegrees = -f.pitchDegrees;
        pose.rollDegrees = f.rollDegrees;
        if (f.hasCameraMatrix) {
            pose.matrix = f.cameraMatrix;
            pose.hasMatrix = true;
        }
        pf.pose = pose;
        poseFrames.push_back(std::move(pf));
    }
    // A single feature pass serves both pose refinement and outlier removal.
    PoseRefineResult refined;
    const bool needFeatures = options.refinePoses || options.dropOutliers;
    if (needFeatures) refined = refinePoses(poseFrames);

    std::vector<double> quality(poseFrames.size(), 1.0);
    int weakCount = 0;
    if (options.dropOutliers && refined.inlierCounts.size() == poseFrames.size()) {
        std::vector<int> positive;
        for (int count : refined.inlierCounts)
            if (count > 0) positive.push_back(count);
        std::sort(positive.begin(), positive.end());
        const double median = positive.empty()
                                  ? 0.0
                                  : static_cast<double>(positive[positive.size() / 2]);
        for (size_t i = 0; i < poseFrames.size(); ++i) {
            const double score = median > 0
                                     ? std::clamp(refined.inlierCounts[i] / (0.5 * median),
                                                  0.0, 1.0)
                                     : 1.0;
            quality[i] = std::max(0.04, score);
            if (quality[i] < 0.3) ++weakCount;
        }
    }

    std::vector<PreparedFrame> prepared;
    prepared.reserve(poseFrames.size());
    for (size_t i = 0; i < poseFrames.size(); ++i) {
        PreparedFrame pf;
        pf.image = poseFrames[i].image;
        pf.intrinsics = poseFrames[i].intrinsics;
        pf.basis = options.refinePoses && refined.rotations.size() == poseFrames.size()
                       ? basisFromMatrix(refined.rotations[i])
                       : basisFromPose(poseFrames[i].pose);
        pf.quality = quality[i];
        prepared.push_back(std::move(pf));
    }
    diagnostics = "multi-frame, " + std::to_string(prepared.size()) + " frames";
    if (weakCount > 0)
        diagnostics += ", downweighted " + std::to_string(weakCount) + " weak frames";
    if (options.refinePoses)
        diagnostics += ", " + std::to_string(refined.edgeCount) + " refined pose edges";
    if (!options.refinePoses && !options.dropOutliers) diagnostics += ", EXIF poses";
    // Per frame exposure gains from the overlap chain.
    std::vector<double> means;
    means.reserve(prepared.size());
    for (const auto& pf : prepared) {
        cv::Mat gray;
        cv::cvtColor(pf.image, gray, cv::COLOR_BGR2GRAY);
        means.push_back(std::max(1e-6, cv::mean(gray)[0]));
    }
    std::vector<std::pair<int, int>> edges;
    for (size_t i = 0; i + 1 < prepared.size(); ++i) {
        edges.emplace_back(static_cast<int>(i), static_cast<int>(i + 1));
        if (i + 2 < prepared.size())
            edges.emplace_back(static_cast<int>(i), static_cast<int>(i + 2));
    }
    const std::vector<float> gains = solveGains(means, edges);
    for (size_t i = 0; i < prepared.size(); ++i)
        prepared[i].gain = gains[i];

    // Graph cut seam carving on reduced warped images, followed by a narrow
    // feather around each cut. This keeps one frame per pixel and suppresses
    // moving objects that a wide cross fade would show twice.
    const SeamWeights* activeSeams = nullptr;
    SeamWeights seams;
    if (options.useSeams) {
        seams = carveSeams(prepared, options.width, options.width / 2, 4);
        if (!seams.empty()) {
            activeSeams = &seams;
            diagnostics += ", graph-cut seam";
        }
    }
    std::vector<LocalWarp> warps;
    if (options.useLocalWarp && needFeatures &&
        refined.samplePixels.size() == poseFrames.size()) {
        warps.reserve(prepared.size());
        for (size_t i = 0; i < prepared.size(); ++i) {
            warps.push_back(estimateLocalWarp(
                refined.samplePixels[i], refined.sampleResiduals[i],
                prepared[i].intrinsics.widthPx, prepared[i].intrinsics.heightPx));
            if (!warps.back().empty()) prepared[i].warp = &warps.back();
        }
        diagnostics += ", local warp";
    }
    RenderOptions ro;
    ro.width = options.width;
    ro.multiband = options.multiband;
    diagnostics += ", gain";
    return renderPanorama(prepared, ro, activeSeams);
}

} // namespace

Panorama stitchSession(const io::InputSession& session,
                       const StitchOptions& options,
                       std::string& diagnostics) {
    if (session.frames.empty()) { diagnostics = "no frames"; return {}; }
    // Use the most recently added source, not the first one. When a video is
    // followed by a still, the user expects the still to be stitched.
    const std::string lastSource = session.frames.back().source;
    std::vector<LensFrame> pair;
    for (auto it = session.frames.rbegin(); it != session.frames.rend(); ++it) {
        if (it->source != lastSource) continue;
        if (it->lens != 0 && it->lens != 1) continue;
        pair.push_back(*it);
        if (pair.size() == 2 && pair[0].lens != pair[1].lens) break;
    }
    if (pair.size() >= 2 && pair[0].lens != pair[1].lens) {
        diagnostics = "source: " + lastSource + ", ";
        return stitchDualFisheye(pair, options, diagnostics);
    }

    int posedCount = 0;
    for (const auto& f : session.frames)
        if (f.hasPose) ++posedCount;
    if (posedCount >= 3)
        return stitchMultiFrame(session.frames, options, diagnostics);

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
