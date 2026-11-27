#include "io/hdr_export.hpp"

#include "io/image_io.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/photo.hpp>

#include <algorithm>
#include <filesystem>

namespace ps::io {

bool exportHdrPanorama(const std::vector<std::string>& paths,
                       const StitchOptions& options,
                       const std::string& output,
                       std::string& error) {
    std::vector<cv::Mat> panoramas;
    std::vector<float> exposures;
    StitchOptions stitch = options;
    stitch.linearOutput = true;

    for (const auto& path : paths) {
        cv::Mat linear;
        double shutter = 0.0;
        double iso = 0.0;
        if (!readStillLinear(path, linear, shutter, iso, error)) {
            error = path + ": " + error;
            return false;
        }
        if (shutter <= 0.0 || iso <= 0.0) {
            error = "missing exposure metadata in " + path;
            return false;
        }
        if (linear.rows != 2 * linear.cols) {
            error = "expected a stacked dual fisheye DNG: " + path;
            return false;
        }
        const int half = linear.rows / 2;
        LensFrame first, second;
        first.image = linear(cv::Rect(0, 0, linear.cols, half)).clone();
        first.lens = 0;
        second.image = linear(cv::Rect(0, half, linear.cols, half)).clone();
        second.lens = 1;
        std::string diagnostics;
        const Panorama pano = stitchDualFisheye({first, second}, stitch, diagnostics);
        if (pano.equirect.empty() || pano.equirect.depth() != CV_32F) {
            error = "linear stitch failed: " + diagnostics;
            return false;
        }
        panoramas.push_back(pano.equirect);
        exposures.push_back(static_cast<float>(shutter / (iso / 100.0)));
    }
    if (panoramas.empty()) { error = "no bracketed frames"; return false; }
    for (size_t i = 1; i < panoramas.size(); ++i) {
        if (panoramas[i].size() != panoramas[0].size()) {
            error = "panorama sizes differ across exposures";
            return false;
        }
    }
    cv::Mat result;
    if (panoramas.size() == 1) {
        result = panoramas[0];
    } else {
        // Debevec's response solver takes 8-bit samples; the radiance map it
        // returns is 32-bit float, which is what we write to EXR.
        std::vector<cv::Mat> samples;
        for (const auto& pano : panoramas) {
            cv::Mat sample;
            pano.convertTo(sample, CV_8UC3, 255.0);
            samples.push_back(sample);
        }
        cv::createMergeDebevec()->process(samples, result, exposures);
    }
    if (result.empty()) { error = "HDR merge failed"; return false; }

    // Debevec's solver leaves the radiance scale arbitrary. Bring it into the
    // range used by common HDRI files without touching the linear response:
    // median luminance around 0.25, bright sun around 10. No clamping.
    std::vector<float> sample;
    sample.reserve(static_cast<size_t>(result.total() / 16));
    const float* values = result.ptr<float>(0);
    for (size_t i = 0; i < result.total(); i += 16) {
        const float v = values[i];
        if (v > 0.0f) sample.push_back(v);
    }
    if (!sample.empty()) {
        const size_t middle = sample.size() / 2;
        std::nth_element(sample.begin(), sample.begin() + middle, sample.end());
        const float median = sample[middle];
        if (median > 1e-6f) result *= (0.25f / median);
    }
    if (!cv::imwrite(output, result)) {
        error = "cannot write " + output + " (OpenEXR support required)";
        return false;
    }
    // A tone-mapped companion makes the HDR bracket inspectable in ordinary
    // image viewers. The EXR itself stays linear and unclamped.
    namespace fs = std::filesystem;
    const fs::path outputPath(output);
    const std::string preview = (outputPath.parent_path() /
                                 (outputPath.stem().string() + "_preview.jpg")).string();
    cv::Mat mapped;
    cv::createTonemapReinhard(1.0f)->process(result, mapped);
    cv::Mat ldr;
    mapped.convertTo(ldr, CV_8UC3, 255.0);
    cv::imwrite(preview, ldr);
    return true;
}

} // namespace ps::io
