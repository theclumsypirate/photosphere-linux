#pragma once

#include "core/stitcher.hpp"

#include <string>
#include <vector>

namespace ps::io {

// Develops each DNG to linear float, dewarping both fisheye lenses into an
// equirectangular HDR image per exposure, then merges the bracket with
// Debevec's method and writes a 32-bit float OpenEXR file.
bool exportHdrPanorama(const std::vector<std::string>& paths,
                       const StitchOptions& options,
                       const std::string& output,
                       std::string& error);

} // namespace ps::io
