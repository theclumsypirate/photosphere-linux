#pragma once

#include <string>
#include <vector>

#include "core/types.hpp"

namespace ps {
struct StitchOptions;
}

namespace ps::io {

struct InputSession {
    std::vector<LensFrame> frames;
    CameraModel model;
    std::string description;
};

// Loads DNG, JPG and INSV files into one ordered lens frame list.
// DNG files contribute two stacked fisheye images per file, side by side JPG
// files contribute two halves, INSV files contribute two frames per timestamp.
bool loadInputs(const std::vector<std::string>& paths, InputSession& out,
                std::string& error, int maxVideoFrames = 0, int videoStep = 1);

struct BatchItem {
    std::string source;
    Panorama panorama;
    std::string diagnostics;
};

// Processes every input file independently, so DNG, JPG and INSV files can be
// dropped in together and each becomes its own panorama.
bool stitchAllInputs(const std::vector<std::string>& paths,
                     const StitchOptions& options,
                     std::vector<BatchItem>& out,
                     std::string& error,
                     int maxVideoFrames = 1);

} // namespace ps::io
