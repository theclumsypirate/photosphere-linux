#pragma once

#include "core/rotation.hpp"

#include <array>

namespace ps::audio {

// First-order ambisonics in AmbiX order: W, Y, Z, X with SN3D normalization.
// The W channel is unaffected by rotation. The three directional channels
// transform as a vector, reordered from Cartesian x, y, z into Y, Z, X.
using FoaMatrix = std::array<double, 16>; // row major, 4x4

FoaMatrix foaRotation(const Mat3& cartesian);

// Rotates planar float samples in place. Each of the four pointers holds
// frameCount consecutive floats, one channel per pointer.
void rotateFoa(float* const channels[4], int frameCount, const FoaMatrix& matrix);

} // namespace ps::audio
