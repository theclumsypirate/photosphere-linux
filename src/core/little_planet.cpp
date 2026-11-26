#include "core/little_planet.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace ps {
namespace {
constexpr double kPi = 3.14159265358979323846;
inline double rad(double d) { return d * kPi / 180.0; }
} // namespace

cv::Mat convertToLittlePlanet(const cv::Mat& equirect, const LittlePlanetOptions& options) {
    if (equirect.empty() || options.outputSize <= 0) return {};
    const int size = options.outputSize;
    const double radius = size / 2.0;
    const double fov = std::clamp(options.fovDegrees, 20.0, 350.0);
    const double scale = 2.0 * std::tan(rad(fov / 2.0) / 2.0);
    const double ct = std::cos(rad(options.tiltDegrees)), st = std::sin(rad(options.tiltDegrees));
    const double cp = std::cos(rad(options.panDegrees)), sp = std::sin(rad(options.panDegrees));
    const double cs = std::cos(rad(options.spinDegrees)), ss = std::sin(rad(options.spinDegrees));
    const double eqW = equirect.cols, eqH = equirect.rows;

    cv::Mat mapX(size, size, CV_32FC1, cv::Scalar(-1.f));
    cv::Mat mapY(size, size, CV_32FC1, cv::Scalar(-1.f));
    for (int y = 0; y < size; ++y) {
        const double dy = y - radius;
        auto* mx = mapX.ptr<float>(y);
        auto* my = mapY.ptr<float>(y);
        for (int x = 0; x < size; ++x) {
            const double dx = x - radius;
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (dist > radius) continue;
            const double sx = dx * cs - dy * ss;
            const double sy = dx * ss + dy * cs;
            const double r = (dist / radius) * scale;
            const double theta = 2.0 * std::atan(r / 2.0);
            const double azimuth = std::atan2(sx, -sy);
            double wx = std::sin(theta) * std::sin(azimuth);
            double wy = -std::cos(theta);
            double wz = std::sin(theta) * std::cos(azimuth);
            const double wy1 = wy * ct - wz * st;
            const double wz1 = wy * st + wz * ct;
            wy = wy1; wz = wz1;
            const double wx2 = wx * cp + wz * sp;
            const double wz2 = -wx * sp + wz * cp;
            wx = wx2; wz = wz2;
            const double len = std::sqrt(wx * wx + wy * wy + wz * wz);
            if (len < 1e-12) continue;
            wx /= len; wy /= len; wz /= len;
            double u = std::atan2(wx, wz) / (2.0 * kPi) + 0.5;
            u -= std::floor(u);
            const double v = std::clamp(std::acos(std::clamp(wy, -1.0, 1.0)) / kPi, 0.0, 1.0);
            mx[x] = static_cast<float>(u * (eqW - 1.0));
            my[x] = static_cast<float>(v * (eqH - 1.0));
        }
    }
    cv::Mat sampled;
    cv::remap(equirect, sampled, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_CONSTANT,
              cv::Scalar(0, 0, 0));
    cv::Mat alpha(size, size, CV_8UC1, cv::Scalar(0));
    cv::circle(alpha, cv::Point(static_cast<int>(radius), static_cast<int>(radius)),
               static_cast<int>(radius), cv::Scalar(255), -1, cv::LINE_8);
    cv::Mat channels[4];
    cv::split(sampled, channels);
    channels[3] = alpha;
    cv::Mat out;
    cv::merge(channels, 4, out);
    return out;
}

} // namespace ps
