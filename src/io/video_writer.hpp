#pragma once

#include <opencv2/core.hpp>

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

    bool open(std::string& error);
    bool write(const cv::Mat& bgr, std::string& error);
    void finish();

    static void setPreferHardwareEncoder(bool enabled);

    Mp4Writer(const Mp4Writer&) = delete;
    Mp4Writer& operator=(const Mp4Writer&) = delete;

private:
    void pumpAudio(double uptoSeconds, bool all);

    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace ps::io
