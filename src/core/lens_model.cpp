#include "core/lens_model.hpp"

#include <algorithm>

namespace ps {
namespace {
constexpr double kPi = 3.14159265358979323846;
inline double tanHalf(double degrees) {
    return std::tan(degrees * kPi / 360.0);
}
} // namespace

FrameIntrinsics FrameIntrinsics::fromFieldOfView(int width, int height,
                                                 double hFovDeg, double vFovDeg) {
    FrameIntrinsics f;
    f.widthPx = width;
    f.heightPx = height;
    f.focalXPx = (width / 2.0) / tanHalf(hFovDeg);
    f.focalYPx = (height / 2.0) / tanHalf(vFovDeg);
    return f;
}

FrameIntrinsics FrameIntrinsics::scaledBy(double scale) const {
    if (scale <= 0.0 || std::abs(scale - 1.0) < 1e-6) return *this;
    FrameIntrinsics out = *this;
    out.focalXPx *= scale;
    out.focalYPx *= scale;
    if (hasRadial) {
        double divisor = scale * scale;
        for (auto& k : out.radial) {
            k /= divisor;
            divisor *= scale * scale;
        }
    }
    return out;
}

bool RadialDistortion::significant() const {
    return std::abs(k_[0]) > 0.0 || std::abs(k_[1]) > 0.0 || std::abs(k_[2]) > 0.0;
}

std::array<double, 3> RadialDistortion::pixelCoefficientsFor(double f) const {
    if (!significant() || !std::isfinite(f) || f <= 0.0) return {0, 0, 0};
    const double f2 = f * f, f4 = f2 * f2, f6 = f4 * f2;
    return {k_[0] / f2, k_[1] / f4, k_[2] / f6};
}

double radialFactor(const std::array<double, 3>& k, double r2) {
    return 1.0 + r2 * (k[0] + r2 * (k[1] + r2 * k[2]));
}

std::array<double, 2> distortPixel(double col, double row, double cx, double cy,
                                   const std::array<double, 3>& k) {
    const double x = col - cx, y = row - cy;
    const double factor = radialFactor(k, x * x + y * y);
    return {cx + x * factor, cy + y * factor};
}

std::array<double, 2> undistortPixel(double col, double row, double cx, double cy,
                                     const std::array<double, 3>& k) {
    const double tx = col - cx, ty = row - cy;
    double x = tx, y = ty;
    for (int i = 0; i < 12; ++i) {
        const double factor = radialFactor(k, x * x + y * y);
        if (factor <= 0.0) return {cx + x, cy + y};
        const double nx = tx / factor, ny = ty / factor;
        const bool settled = std::abs(nx - x) < 1e-4 && std::abs(ny - y) < 1e-4;
        x = nx; y = ny;
        if (settled) break;
    }
    return {cx + x, cy + y};
}

} // namespace ps
