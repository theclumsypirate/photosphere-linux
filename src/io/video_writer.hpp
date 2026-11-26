#pragma once

#include <opencv2/core.hpp>

#include <string>

namespace ps::io {

// Small MP4/H.264 writer for equirectangular panorama sequences.
class Mp4Writer {
public:
    Mp4Writer(const std::string& path, int width, int height, double fps);
    ~Mp4Writer();

    bool open(std::string& error);
    bool write(const cv::Mat& bgr, std::string& error);
    void finish();

    Mp4Writer(const Mp4Writer&) = delete;
    Mp4Writer& operator=(const Mp4Writer&) = delete;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace ps::io
