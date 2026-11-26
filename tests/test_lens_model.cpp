#include "core/lens_model.hpp"

#include <cmath>
#include <iostream>

namespace ps {

int runLensModelTests() {
    int failures = 0;
    const auto intr = FrameIntrinsics::fromFieldOfView(4000, 3000, 90.0, 70.0);
    if (std::abs(intr.focalXPx - 2000.0) > 1e-6) ++failures;
    if (std::abs(intr.centerXPx() - 2000.0) > 1e-6) ++failures;
    const auto scaled = intr.scaledBy(2.0);
    if (std::abs(scaled.focalXPx - 4000.0) > 1e-6) ++failures;

    const RadialDistortion distortion{{0.1, -0.02, 0.003}};
    const auto k = distortion.pixelCoefficientsFor(1000.0);
    if (std::abs(k[0] - 1e-7) > 1e-12) ++failures;
    if (!distortion.significant()) ++failures;
    if (RadialDistortion{{0, 0, 0}}.significant()) ++failures;

    const std::array<double, 3> zero{0, 0, 0};
    const auto same = distortPixel(2400.0, 1700.0, 2000.0, 1500.0, zero);
    if (std::abs(same[0] - 2400.0) > 1e-9 || std::abs(same[1] - 1700.0) > 1e-9) ++failures;
    const auto round = undistortPixel(same[0], same[1], 2000.0, 1500.0, zero);
    if (std::abs(round[0] - 2400.0) > 1e-9 || std::abs(round[1] - 1700.0) > 1e-9) ++failures;

    const auto barrel = distortPixel(2200.0, 1500.0, 2000.0, 1500.0, k);
    if (!(barrel[0] > 2200.0)) ++failures;
    const auto back = undistortPixel(barrel[0], barrel[1], 2000.0, 1500.0, k);
    if (std::abs(back[0] - 2200.0) > 0.01) ++failures;
    return failures;
}

} // namespace ps
