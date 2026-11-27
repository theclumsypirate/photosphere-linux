#include "audio/ambisonics.hpp"

#include "core/rotation.hpp"

#include <cmath>
#include <cstdio>

namespace ps {

int runAmbisonicsTests() {
    int failures = 0;
    auto check = [&](bool condition, const char* name) {
        if (!condition) {
            std::printf("  FAIL %s\n", name);
            ++failures;
        }
    };

    const Mat3 identity = rotation::identity();
    const auto unchanged = audio::foaRotation(identity);
    for (int i = 0; i < 16; ++i)
        check(std::abs(unchanged[static_cast<size_t>(i)] -
                       (i % 5 == 0 ? 1.0 : 0.0)) < 1e-9,
              "identity matrix");

    const Mat3 yaw90 = rotation::rotationAboutAxis({0.0, 0.0, 1.0},
                                                   3.14159265358979323846 / 2.0);
    const auto rotated = audio::foaRotation(yaw90);
    const float source[4] = {1.0f, 0.0f, 0.0f, 2.0f};
    float w = source[0], y = source[1], z = source[2], x = source[3];
    float* channels[4] = {&w, &y, &z, &x};
    audio::rotateFoa(channels, 1, rotated);
    check(std::abs(w - 1.0f) < 1e-5f, "omni channel is invariant");
    check(std::abs(y - 2.0f) < 1e-5f, "X becomes Y on a 90 degree yaw");
    check(std::abs(x) < 1e-5f, "Y becomes minus X on a 90 degree yaw");
    check(std::abs(z) < 1e-5f, "up channel is invariant");

    const float normAfter = std::sqrt(y * y + z * z + x * x);
    check(std::abs(normAfter - 2.0f) < 1e-5f, "directional norm is preserved");
    return failures;
}

} // namespace ps
