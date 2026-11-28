#include "core/shader_preview.hpp"

#include "core/rotation.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ps {
namespace {

constexpr double kPi = 3.14159265358979323846;

Vec3 rotateView(const Vec3& v, double yawRad, double pitchRad) {
    const double cy = std::cos(yawRad), sy = std::sin(yawRad);
    const double cp = std::cos(pitchRad), sp = std::sin(pitchRad);
    const Vec3 yawed{sy * v[0] + cy * v[2], v[1], cy * v[0] - sy * v[2]};
    return {yawed[0], cp * yawed[1] - sp * yawed[2],
            sp * yawed[1] + cp * yawed[2]};
}

} // namespace

cv::Mat renderFisheyeView(const cv::Mat& sideBySide,
                          double yawDegrees, double pitchDegrees,
                          double fovDegrees, double lensFovDegrees,
                          int width, int height) {
    if (sideBySide.empty() || width <= 0 || height <= 0) return {};
    const int halfWidth = sideBySide.cols / 2;
    if (halfWidth <= 0) return {};
    const cv::Mat left = sideBySide(cv::Rect(0, 0, halfWidth, sideBySide.rows));
    const cv::Mat right = sideBySide(cv::Rect(halfWidth, 0,
                                              halfWidth, sideBySide.rows));

    const double aspect = static_cast<double>(width) / height;
    const double tanHalf = std::tan(fovDegrees * kPi / 360.0);
    const double lensHalf = std::max(1.0, lensFovDegrees * kPi / 360.0);
    const double yawRad = yawDegrees * kPi / 180.0;
    const double pitchRad = pitchDegrees * kPi / 180.0;

    cv::Mat mapXA(height, width, CV_32FC1), mapYA(height, width, CV_32FC1);
    cv::Mat mapXB(height, width, CV_32FC1), mapYB(height, width, CV_32FC1);
    cv::Mat weightA(height, width, CV_32FC1), weightB(height, width, CV_32FC1);
    for (int row = 0; row < height; ++row) {
        const double cy = -((row + 0.5) / height - 0.5) * 2.0 * tanHalf;
        auto* ax = mapXA.ptr<float>(row);
        auto* ay = mapYA.ptr<float>(row);
        auto* bx = mapXB.ptr<float>(row);
        auto* by = mapYB.ptr<float>(row);
        auto* wa = weightA.ptr<float>(row);
        auto* wb = weightB.ptr<float>(row);
        for (int col = 0; col < width; ++col) {
            const double cx = ((col + 0.5) / width - 0.5) * 2.0 * tanHalf * aspect;
            const Vec3 direction = rotateView({cx, cy, 1.0}, yawRad, pitchRad);
            const double norm = rotation::norm3(direction);
            const Vec3 d = rotation::scale3(direction, 1.0 / norm);

            const double thetaA = std::acos(std::clamp(d[2], -1.0, 1.0));
            const double phiA = std::atan2(d[0], d[1]);
            const double rA = thetaA / lensHalf;
            ax[col] = static_cast<float>((0.5 + 0.5 * rA * std::sin(phiA)) * halfWidth - 0.5);
            ay[col] = static_cast<float>((0.5 - 0.5 * rA * std::cos(phiA)) * left.rows - 0.5);

            const double thetaB = std::acos(std::clamp(-d[2], -1.0, 1.0));
            const double phiB = std::atan2(-d[0], d[1]);
            const double rB = thetaB / lensHalf;
            bx[col] = static_cast<float>((0.5 + 0.5 * rB * std::sin(phiB)) * halfWidth - 0.5);
            by[col] = static_cast<float>((0.5 - 0.5 * rB * std::cos(phiB)) * right.rows - 0.5);

            const double edgeA = std::clamp((1.0 - rA) / 0.08, 0.0, 1.0);
            const double edgeB = std::clamp((1.0 - rB) / 0.08, 0.0, 1.0);
            wa[col] = static_cast<float>(edgeA);
            wb[col] = static_cast<float>(edgeB);
        }
    }
    cv::Mat warpedA, warpedB;
    cv::remap(left, warpedA, mapXA, mapYA, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    cv::remap(right, warpedB, mapXB, mapYB, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    cv::Mat warpedAF, warpedBF;
    warpedA.convertTo(warpedAF, CV_32FC3);
    warpedB.convertTo(warpedBF, CV_32FC3);
    cv::Mat weight3A, weight3B;
    cv::merge(std::vector<cv::Mat>{weightA, weightA, weightA}, weight3A);
    cv::merge(std::vector<cv::Mat>{weightB, weightB, weightB}, weight3B);
    cv::Mat sum, weights, partA, partB;
    cv::multiply(warpedAF, weight3A, partA);
    cv::multiply(warpedBF, weight3B, partB);
    cv::add(partA, partB, sum);
    cv::add(weight3A, weight3B, weights);
    weights.setTo(cv::Scalar(1e-3f, 1e-3f, 1e-3f), weights < 1e-3f);
    cv::divide(sum, weights, sum);
    return sum;
}

} // namespace ps
