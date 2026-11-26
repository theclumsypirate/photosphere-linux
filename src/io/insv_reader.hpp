#pragma once

#include <string>
#include <vector>

#include "core/types.hpp"

namespace ps::io {

// True when the file has the Insta360 INSV magic. Such files are ordinary MP4
// containers with a proprietary telemetry box appended.
bool isInsvFile(const std::string& path);

// Decodes both fisheye video tracks. Frames are paired by decode order, which
// matches the two lenses in the tested file. maxFrames <= 0 means all frames.
bool readInsv(const std::string& path, std::vector<LensFrame>& out,
              int maxFrames, int step, std::string& error);

// Average frame rate of the first video track, or 25 when unavailable.
double insvFrameRate(const std::string& path);

// Duration in seconds, or 0 when unavailable.
double insvDuration(const std::string& path);

// Decodes only one frame pair near each requested timestamp by seeking
// instead of walking the whole file. Much faster for sampling a long video.
bool readInsvAtTimes(const std::string& path, const std::vector<double>& times,
                     std::vector<LensFrame>& out, std::string& error);

} // namespace ps::io
