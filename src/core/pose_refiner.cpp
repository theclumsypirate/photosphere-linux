#include "core/pose_refiner.hpp"

#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace ps {
namespace {

struct Features {
    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
};

Features extractFeatures(const cv::Mat& image, int longEdge = 640) {
    Features f;
    if (image.empty()) return f;
    const double scale = static_cast<double>(longEdge) / std::max(image.cols, image.rows);
    cv::Mat small;
    if (scale < 1.0)
        cv::resize(image, small, cv::Size(), scale, scale, cv::INTER_AREA);
    else
        small = image;
    cv::Mat gray;
    cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
    cv::Ptr<cv::ORB> orb = cv::ORB::create(1800, 1.2f, 8, 31, 0, 2, cv::ORB::HARRIS_SCORE, 31, 20);
    orb->detectAndCompute(gray, cv::noArray(), f.keypoints, f.descriptors);
    for (auto& kp : f.keypoints) kp.pt = kp.pt / static_cast<float>(scale);
    return f;
}

Vec3 bearingFromPixel(const cv::KeyPoint& kp, const FrameIntrinsics& in, double scale) {
    const double fx = in.focalXPx * scale;
    const double fy = in.focalYPx * scale;
    const double x = (kp.pt.x - in.centerXPx() * scale) / fx;
    // basisToMatrix stores [right, -up, forward], so the exchange frame's
    // vertical axis points down and the pixel row offset keeps its sign.
    const double y = (kp.pt.y - in.centerYPx() * scale) / fy;
    return rotation::normalize3({x, y, 1.0});
}

} // namespace

PoseRefineResult refinePoses(const std::vector<PoseFrame>& frames,
                             double thresholdDegrees, int maxIterations) {
    PoseRefineResult result;
    const size_t n = frames.size();
    if (n < 2) {
        result.rotations.assign(n, rotation::identity());
        return result;
    }
    std::vector<Features> features(n);
    std::vector<double> scales(n, 1.0);
    for (size_t i = 0; i < n; ++i) {
        scales[i] = static_cast<double>(std::max(frames[i].image.cols, frames[i].image.rows)) > 640
                        ? 640.0 / std::max(frames[i].image.cols, frames[i].image.rows)
                        : 1.0;
        features[i] = extractFeatures(frames[i].image);
    }
    std::vector<Mat3> initial(n);
    for (size_t i = 0; i < n; ++i) initial[i] = basisToMatrix(basisFromPose(frames[i].pose));

    std::mt19937 rng(20261126u);
    std::vector<rotation::RotationEdge> edges;
    const double thresholdRad = thresholdDegrees * M_PI / 180.0;
    cv::Ptr<cv::BFMatcher> matcher = cv::BFMatcher::create(cv::NORM_HAMMING);
    for (size_t a = 0; a + 1 < n; ++a) {
        for (size_t b = a + 1; b < n; ++b) {
            if (features[a].descriptors.empty() || features[b].descriptors.empty()) continue;
            std::vector<std::vector<cv::DMatch>> knn;
            matcher->knnMatch(features[a].descriptors, features[b].descriptors, knn, 2);
            std::vector<Vec3> from, to;
            for (const auto& m : knn) {
                if (m.size() < 2 || m[0].distance > 0.72f * m[1].distance) continue;
                from.push_back(bearingFromPixel(features[a].keypoints[m[0].queryIdx],
                                                frames[a].intrinsics, scales[a]));
                to.push_back(bearingFromPixel(features[b].keypoints[m[0].trainIdx],
                                              frames[b].intrinsics, scales[b]));
            }
            if (from.size() < 8) continue;
            auto estimate = rotation::estimateRotation(from, to, thresholdRad,
                                                        maxIterations, rng);
            if (estimate.inliers.size() < 8) continue;
            // The estimate maps bearings of a into b. Convert it into the
            // [right, -up, forward] exchange convention used by CameraBasis.
            rotation::RotationEdge edge;
            edge.from = static_cast<int>(a);
            edge.to = static_cast<int>(b);
            edge.rotation = estimate.rotation;
            edge.weight = 1.0 / (1.0 + estimate.residualRadians * 10.0);
            edges.push_back(edge);
            ++result.edgeCount;
            result.totalInliers += static_cast<int>(estimate.inliers.size());
        }
    }
    result.rotations = rotation::averageRotations(initial, edges, 24);
    return result;
}

} // namespace ps
