#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "io/frame_store.hpp"

namespace ps {

struct StitchOptions {
    int width = 4096;
    double fisheyeFovDegrees = 195.0;
    // Insta360 lens images are mounted upside down relative to the world
    // frame, so the sampled fisheye coordinate is rotated by 180 degrees.
    bool flipLenses = true;
    bool multiband = true;
    bool refinePoses = false;
    bool useSeams = false;
    bool dropOutliers = true;
    bool useLocalWarp = true;
    std::function<void(int, int)> onProgress;
    bool linearOutput = false;
    int maxFrames = 0;
    const std::atomic<bool>* cancelFlag = nullptr;
};

// Chooses between the dual fisheye path (Insta360 DNG/JPG/INSV) and the
// generic multi-frame path (a ring of overlapping frames).
Panorama stitchSession(const io::InputSession& session,
                       const StitchOptions& options,
                       std::string& diagnostics);

Panorama stitchDualFisheye(const std::vector<LensFrame>& pair,
                           const StitchOptions& options,
                           std::string& diagnostics);

} // namespace ps
