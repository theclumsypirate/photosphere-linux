#include "audio/ambisonics.hpp"

namespace ps::audio {

FoaMatrix foaRotation(const Mat3& r) {
    // ACN component 1 is Y (Cartesian y), 2 is Z (Cartesian z) and 3 is X
    // (Cartesian x). Reindex the Cartesian rotation accordingly.
    FoaMatrix m{};
    m[0] = 1.0;
    m[5] = r[4]; m[6] = r[5]; m[7] = r[3];
    m[9] = r[7]; m[10] = r[8]; m[11] = r[6];
    m[13] = r[1]; m[14] = r[2]; m[15] = r[0];
    return m;
}

void rotateFoa(float* const channels[4], int frameCount, const FoaMatrix& m) {
    if (frameCount <= 0) return;
    for (int i = 0; i < frameCount; ++i) {
        const float w = channels[0][i];
        const float y = channels[1][i];
        const float z = channels[2][i];
        const float x = channels[3][i];
        channels[0][i] = static_cast<float>(m[0] * w + m[1] * y + m[2] * z + m[3] * x);
        channels[1][i] = static_cast<float>(m[4] * w + m[5] * y + m[6] * z + m[7] * x);
        channels[2][i] = static_cast<float>(m[8] * w + m[9] * y + m[10] * z + m[11] * x);
        channels[3][i] = static_cast<float>(m[12] * w + m[13] * y + m[14] * z + m[15] * x);
    }
}

} // namespace ps::audio
