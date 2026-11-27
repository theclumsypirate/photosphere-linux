#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

#include "core/types.hpp"

namespace ps::io {

struct RawOptions {
    double exposureEV = 0.0; // added on top of the raw development
    bool autoBright = true;  // current default, keeps existing output
};

void setRawOptions(const RawOptions& options);
RawOptions rawOptions();

// Reads a still image. DNG files yield two lens images (the raw file holds two
// stacked fisheye crops). Side by side JPG files also yield two lens images.
// Plain JPEG/PNG files yield one.
bool readStill(const std::string& path, std::vector<cv::Mat>& out,
               std::string& error, CameraModel* model = nullptr);

// Reads an Insta360 calibration string found in the EXIF UserComment tag.
// The format is not public; the fields used here are the three principal
// point / radius values and the reported sensor dimensions.
bool parseInsta360Calibration(const std::string& comment, CameraModel& model);

// Develops a RAW file into linear 32-bit float RGB (0..1 range). Used by the
// HDR bracket merge and by 32-bit EXR export.
bool readStillLinear(const std::string& path, cv::Mat& outLinear,
                     double& exposureSeconds, double& iso,
                     std::string& error);

} // namespace ps::io
