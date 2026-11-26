#include "core/rotation.hpp"

#include <cmath>
#include <iostream>

namespace ps {

namespace {
bool close(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }
}

int runRotationTests() {
    int failures = 0;
    const auto i = rotation::identity();
    for (int k = 0; k < 9; ++k)
        if (!close(i[k], k % 4 == 0 ? 1.0 : 0.0)) ++failures;

    const Mat3 r = rotation::rotationAboutAxis({0, 0, 1}, M_PI / 2);
    const Vec3 v = rotation::apply(r, {1, 0, 0});
    if (!close(v[0], 0.0, 1e-9) || !close(v[1], 1.0, 1e-9)) ++failures;
    if (!close(rotation::angle(i, rotation::multiply(r, rotation::transpose(r))), 0.0, 1e-9))
        ++failures;

    const Mat3 quarter = rotation::rotationBetween({0, 1, 0}, {1, 0, 0});
    const auto back = rotation::apply(quarter, {0, 1, 0});
    if (!close(back[0], 1.0, 1e-9) || !close(back[1], 0.0, 1e-9)) ++failures;

    const Mat3 basis = rotation::basisRotation({1, 0, 0}, {0, 1, 0},
                                               {0, 1, 0}, {-1, 0, 0});
    const auto b1 = rotation::apply(basis, {1, 0, 0});
    if (!close(b1[0], 0.0, 1e-9) || !close(b1[1], 1.0, 1e-9)) ++failures;

    const auto q = rotation::matrixToQuaternion(quarter);
    const auto fromQ = rotation::quaternionToMatrix(q);
    for (int k = 0; k < 9; ++k)
        if (!close(fromQ[k], quarter[k], 1e-9)) ++failures;
    return failures;
}

} // namespace ps
