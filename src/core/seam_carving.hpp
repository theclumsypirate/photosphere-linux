#pragma once

#include "core/renderer.hpp"
#include "core/seam.hpp"

namespace ps {

// Assigns each canvas pixel to the frame that should paint it, using
// OpenCV's graph cut seam finder on reduced warped images. The result is a
// narrow feather around each cut.
SeamWeights carveSeams(const std::vector<PreparedFrame>& frames,
                       int canvasWidth, int canvasHeight,
                       int scale = 4);

} // namespace ps
