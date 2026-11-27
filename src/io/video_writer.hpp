#pragma once

#include "core/rotation.hpp"

#include <opencv2/core.hpp>

#include <functional>
#include <string>

namespace ps::io {

// Small MP4/H.264 writer for equirectangular panorama sequences.
class Mp4Writer {
public:
    Mp4Writer(const std::string& path, int width, int height, double fps);
    ~Mp4Writer();

    // Copies the audio track of inputPath into the output. The range is
    // trimmed to [startTime, endTime] seconds (endTime = 0 means full file).
    void setAudioSource(const std::string& inputPath, double startTime,
                        double endTime);

    // When the source audio is first-order ambisonics, rotate the sound field
    // by the camera orientation at each audio frame. The callback receives
    // video time in seconds and returns a camera-to-world rotation.
    void setAmbisonicRotation(std::function<Mat3(double)> rotationAt);

    // Treat a four-channel audio track as first-order ambisonics even when the
    // container does not tag it. Useful for cameras that omit the layout.
    void setForceAmbisonic(bool enabled);

    bool open(std::string& error);
    bool write(const cv::Mat& bgr, std::string& error);
    void finish();

    static void setPreferHardwareEncoder(bool enabled);

    // CRF for software encoders, bitrate in bits/s when greater than zero.
    void setVideoQuality(double crf, int64_t bitrate);
    void setSphericalMetadata(bool enabled);
    void setHevc(bool enabled);

    Mp4Writer(const Mp4Writer&) = delete;
    Mp4Writer& operator=(const Mp4Writer&) = delete;

private:
    void pumpAudio(double uptoSeconds, bool all);

    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace ps::io
