#include "core/stabilize.hpp"

#include "core/spherical.hpp"

#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ps {
namespace {

constexpr double kPi = 3.14159265358979323846;
const Vec3 kDown{0.0, 0.0, -1.0};

Vec3 normalizeOr(const Vec3& v, const Vec3& fallback) {
    const double n = rotation::norm3(v);
    if (!(n > 1e-9) || !std::isfinite(n)) return fallback;
    return rotation::scale3(v, 1.0 / n);
}

Mat3 slerp(const Mat3& from, const Mat3& to, double factor) {
    const Quat a = rotation::matrixToQuaternion(from);
    Quat b = rotation::matrixToQuaternion(to);
    double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0.0) {
        dot = -dot;
        for (int i = 0; i < 4; ++i) b[i] = -b[i];
    }
    dot = std::clamp(dot, -1.0, 1.0);
    if (dot > 0.9995) {
        Quat q;
        for (int i = 0; i < 4; ++i) q[i] = a[i] + factor * (b[i] - a[i]);
        const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (n > 1e-9)
            for (int i = 0; i < 4; ++i) q[i] /= n;
        return rotation::quaternionToMatrix(q);
    }
    const double theta = std::acos(dot);
    const double sinTheta = std::sin(theta);
    const double sa = std::sin((1.0 - factor) * theta) / sinTheta;
    const double sb = std::sin(factor * theta) / sinTheta;
    Quat q;
    for (int i = 0; i < 4; ++i) q[i] = sa * a[i] + sb * b[i];
    return rotation::quaternionToMatrix(q);
}

Vec3 rotateVec(const Mat3& m, const Vec3& v) { return rotation::apply(m, v); }

Mat3 interpolate(const std::vector<double>& times,
                 const std::vector<Mat3>& values, double timeSeconds) {
    if (times.empty() || values.empty()) return rotation::identity();
    if (timeSeconds <= times.front()) return values.front();
    if (timeSeconds >= times.back()) return values.back();
    const auto next = std::lower_bound(times.begin(), times.end(), timeSeconds);
    if (next == times.begin()) return values.front();
    const size_t b = static_cast<size_t>(next - times.begin());
    const size_t a = b - 1;
    const double span = times[b] - times[a];
    const double factor = span > 0.0 ? (timeSeconds - times[a]) / span : 0.0;
    return slerp(values[a], values[b], factor);
}

} // namespace

bool Stabilizer::load(const io::InsvTelemetry& telemetry,
                      const StabilizeOptions& options, std::string& error) {
    times_.clear();
    rotations_.clear();
    orientations_.clear();
    const size_t count = telemetry.samples.size();
    if (count < 2) {
        error = "not enough IMU samples to stabilize";
        return false;
    }

    // Integrate angular velocity into a camera-to-pano rotation. The measured
    // gravity slowly pulls the integration back to a level horizon.
    std::vector<Mat3> orientation(count);
    std::vector<Vec3> gravity(count);
    Mat3 current = rotation::identity();
    for (size_t i = 0; i < count; ++i) {
        const auto& sample = telemetry.samples[i];
        const double dt = i + 1 < count
                              ? telemetry.secondsAt(i + 1) - telemetry.secondsAt(i)
                              : 1.0 / std::max(1.0, telemetry.fps);
        const Vec3 omega{sample.gyro[0], sample.gyro[1], sample.gyro[2]};
        const double speed = rotation::norm3(omega);
        if (speed > 1e-6 && dt > 0.0 && std::isfinite(dt)) {
            const Mat3 step = rotation::rotationAboutAxis(
                rotation::scale3(omega, 1.0 / speed), speed * dt);
            current = rotation::multiply(current, step);
        }
        const Vec3 measured = normalizeOr(
            {sample.acc[0], sample.acc[1], sample.acc[2]}, kDown);
        const Vec3 predicted = rotateVec(rotation::transpose(current), kDown);
        const Mat3 correction = rotation::rotationBetween(measured, predicted);
        current = rotation::multiply(current, slerp(rotation::identity(), correction, 0.015));
        orientation[i] = current;
        gravity[i] = normalizeOr(rotateVec(current, measured), kDown);
    }

    // A long time constant keeps intended pans while short shakes are removed.
    const double tau = std::max(0.01, options.smoothSeconds);
    std::vector<Mat3> smooth(count);
    std::vector<Vec3> level(count);
    smooth[0] = orientation[0];
    level[0] = gravity[0];
    for (size_t i = 1; i < count; ++i) {
        const double dt = std::max(0.0, telemetry.secondsAt(i) - telemetry.secondsAt(i - 1));
        const double alpha = dt / (tau + dt);
        smooth[i] = slerp(smooth[i - 1], orientation[i], alpha);
        level[i] = rotation::add3(
            rotation::scale3(level[i - 1], 1.0 - alpha),
            rotation::scale3(gravity[i], alpha));
        level[i] = normalizeOr(level[i], kDown);
    }

    times_.reserve(count);
    rotations_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const Mat3 horizon = rotation::rotationBetween(level[i], kDown);
        Mat3 result = slerp(rotation::identity(), horizon,
                            std::clamp(options.horizonStrength, 0.0, 1.0));
        if (options.mode == StabilizeMode::FlowState) {
            const Mat3 shake = rotation::multiply(
                rotation::transpose(smooth[i]), orientation[i]);
            const double strength = std::clamp(
                0.5 * (options.yawStrength + options.pitchStrength), 0.0, 1.0);
            result = rotation::multiply(slerp(rotation::identity(), shake, strength),
                                        result);
        }
        times_.push_back(telemetry.secondsAt(i) - telemetry.videoOffsetSeconds);
        rotations_.push_back(result);
        orientations_.push_back(orientation[i]);
    }
    return true;
}

Mat3 Stabilizer::rotationAt(double timeSeconds) const {
    return interpolate(times_, rotations_, timeSeconds);
}

Mat3 Stabilizer::orientationAt(double timeSeconds) const {
    return interpolate(times_, orientations_, timeSeconds);
}

double Stabilizer::durationSeconds() const {
    return times_.empty() ? 0.0 : times_.back() - times_.front();
}

double Stabilizer::rollAt(double timeSeconds) const {
    const Mat3 r = rotationAt(timeSeconds);
    const double signedSine = std::clamp(-r[3], -1.0, 1.0);
    return rotation::angle(r, rotation::identity()) * 180.0 / kPi *
           (signedSine < 0.0 ? -1.0 : 1.0);
}

cv::Mat rotateEquirect(const cv::Mat& equirect, const Mat3& rotation) {
    if (equirect.empty()) return {};
    const Mat3 inverse = rotation::transpose(rotation);
    cv::Mat mapX(equirect.rows, equirect.cols, CV_32FC1);
    cv::Mat mapY(equirect.rows, equirect.cols, CV_32FC1);
    // The equirectangular grid is fixed, so its trigonometry is computed once
    // per row and column instead of once per pixel.
    std::vector<double> sinLon(equirect.cols), cosLon(equirect.cols);
    for (int col = 0; col < equirect.cols; ++col) {
        const double lon = equirect::longitudeDegrees(col, equirect.cols) *
                           kPi / 180.0;
        sinLon[col] = std::sin(lon);
        cosLon[col] = std::cos(lon);
    }
    std::vector<double> sinLat(equirect.rows), cosLat(equirect.rows);
    for (int row = 0; row < equirect.rows; ++row) {
        const double lat = equirect::latitudeDegrees(row, equirect.rows) *
                           kPi / 180.0;
        sinLat[row] = std::sin(lat);
        cosLat[row] = std::cos(lat);
    }
    cv::parallel_for_(cv::Range(0, equirect.rows), [&](const cv::Range& range) {
        for (int row = range.start; row < range.end; ++row) {
            const double cl = cosLat[row], sl = sinLat[row];
            auto* mx = mapX.ptr<float>(row);
            auto* my = mapY.ptr<float>(row);
            for (int col = 0; col < equirect.cols; ++col) {
                const Vec3 direction{sinLon[col] * cl, cosLon[col] * cl, sl};
                const Vec3 source = rotateVec(inverse, direction);
                mx[col] = static_cast<float>(equirect::columnFor(
                    equirect::longitudeOf(source), equirect.cols));
                my[col] = static_cast<float>(std::clamp(
                    equirect::rowFor(equirect::latitudeOf(source), equirect.rows),
                    0.0, static_cast<double>(equirect.rows - 1)));
            }
        }
    });
    cv::Mat out;
    cv::remap(equirect, out, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_WRAP);
    return out;
}

cv::Mat Stabilizer::apply(const cv::Mat& equirect, double timeSeconds) const {
    return rotateEquirect(equirect, rotationAt(timeSeconds));
}

} // namespace ps
