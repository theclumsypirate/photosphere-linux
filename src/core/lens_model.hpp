#pragma once

#include <array>
#include <cmath>

namespace ps {

// Pinhole intrinsics with an optional Brown-Conrady radial term whose
// coefficients are already expressed in pixels of this frame.
struct FrameIntrinsics {
    int widthPx = 1;
    int heightPx = 1;
    double focalXPx = 1.0;
    double focalYPx = 1.0;
    std::array<double, 3> radial{0, 0, 0}; // in pixel units
    bool hasRadial = false;

    double centerXPx() const { return widthPx / 2.0; }
    double centerYPx() const { return heightPx / 2.0; }
    double focalPx() const { return std::sqrt(focalXPx * focalYPx); }

    static FrameIntrinsics fromFieldOfView(int width, int height,
                                           double hFovDeg, double vFovDeg);
    FrameIntrinsics scaledBy(double scale) const;
};

// Unitless k1/k2/k3 applied to focal-normalized coordinates.
class RadialDistortion {
public:
    explicit RadialDistortion(std::array<double, 3> k) : k_(k) {}
    bool significant() const;
    std::array<double, 3> pixelCoefficientsFor(double focalPx) const;

private:
    std::array<double, 3> k_;
};

double radialFactor(const std::array<double, 3>& k, double r2);
std::array<double, 2> distortPixel(double col, double row, double cx, double cy,
                                   const std::array<double, 3>& k);
std::array<double, 2> undistortPixel(double col, double row, double cx, double cy,
                                     const std::array<double, 3>& k);

} // namespace ps
