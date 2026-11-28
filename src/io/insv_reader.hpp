#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/types.hpp"

namespace ps::io {

// True for a video container that libav can demux: MP4/INSV/MOV/MKV/WebM.
bool isVideoFile(const std::string& path);

// Kept for compatibility. Same result as isVideoFile.
bool isInsvFile(const std::string& path);

// Decodes both fisheye video tracks. Frames are paired by decode order, which
// matches the two lenses in the tested file. maxFrames <= 0 means all frames.
bool readInsv(const std::string& path, std::vector<LensFrame>& out,
              int maxFrames, int step, std::string& error);

// Average frame rate of the first video track, or 25 when unavailable.
double insvFrameRate(const std::string& path);

// Duration in seconds, or 0 when unavailable.
double insvDuration(const std::string& path);

// True for GoPro MAX .360 files: two video tracks holding the top and bottom
// halves of an EAC cubemap rather than two fisheye lenses.
bool isEacVideo(const std::string& path);

// Decodes only one frame pair near each requested timestamp by seeking
// instead of walking the whole file. Much faster for sampling a long video.
bool readInsvAtTimes(const std::string& path, const std::vector<double>& times,
                     std::vector<LensFrame>& out, std::string& error);

// Single-pass streaming variant. Frames are handed to the callback as soon as
// a lens pair is ready, so long videos never accumulate in memory. Returning
// false from the callback stops the decode.
using InsvFrameCallback = std::function<bool(const std::vector<LensFrame>&)>;
bool streamInsvAtTimes(const std::string& path, const std::vector<double>& times,
                       const InsvFrameCallback& callback, std::string& error);

// Returns a seek target in seconds, or -1 when no seek is pending. Called
// from the decode thread so a player can seek without reopening the file.
using InsvSeekRequest = std::function<double()>;

// Sequential playback decode: reads packets in file order with a single seek
// at the start, so the first frame arrives as fast as a media player's. An
// optional seek request is handled inside the same read loop.
bool streamInsvSequential(const std::string& path, double fromSeconds,
                          const InsvFrameCallback& callback, std::string& error,
                          int scaleWidth = 0,
                          InsvSeekRequest seekRequest = {});

} // namespace ps::io
