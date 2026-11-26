#pragma once

#include <opencv2/core.hpp>

#include <vector>

#include "core/lens_model.hpp"
#include "core/rotation.hpp"
#include "core/spherical.hpp"

namespace ps {

struct PoseFrame {
    cv::Mat image;                // CV_8UC3
    FrameIntrinsics intrinsics;
    CameraPose pose;
};

struct PoseRefineResult {
    std::vector<Mat3> rotations;
    std::vector<int> inlierCounts; // matched inliers per frame, for quality filtering
    std::vector<std::vector<cv::Point2f>> samplePixels;
    std::vector<std::vector<cv::Point2f>> sampleResiduals;
    int edgeCount = 0;
    int totalInliers = 0;
};

// ORB + two-point rotation RANSAC + Gauss-Seidel graph averaging, modeled on
// PoseRefiner.kt. Frame 0 is held fixed as the reference.
PoseRefineResult refinePoses(const std::vector<PoseFrame>& frames,
                             double thresholdDegrees = 2.0,
                             int maxIterations = 250);

} // namespace ps
