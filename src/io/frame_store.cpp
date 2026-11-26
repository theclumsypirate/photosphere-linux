#include "io/frame_store.hpp"

#include "core/stitcher.hpp"
#include "io/exif_pose.hpp"
#include "io/image_io.hpp"
#include "io/insv_reader.hpp"

#include <algorithm>

namespace ps::io {
namespace {

bool hasExtension(const std::string& path, const std::string& ext) {
    if (path.size() <= ext.size()) return false;
    std::string tail = path.substr(path.size() - ext.size());
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == ext;
}

} // namespace

bool loadInputs(const std::vector<std::string>& paths, InputSession& out,
                std::string& error, int maxVideoFrames, int videoStep) {
    out = {};
    int sequence = 0;
    for (const auto& path : paths) {
        if (isInsvFile(path)) {
            std::vector<LensFrame> frames;
            if (!readInsv(path, frames, maxVideoFrames, videoStep, error)) return false;
            for (auto& f : frames) {
                f.sequence = sequence++;
                out.frames.push_back(std::move(f));
            }
            out.description += "INSV " + path + " ";
            continue;
        }
        std::vector<cv::Mat> images;
        CameraModel model;
        if (!readStill(path, images, error, &model)) return false;
        if (hasExtension(path, ".dng") && images.size() == 1) {
            const cv::Mat& raw = images.front();
            if (raw.rows % 2 != 0) { error = "DNG height is not twice its width"; return false; }
            const int half = raw.rows / 2;
            for (int lens = 0; lens < 2; ++lens) {
                LensFrame f;
                f.image = raw(cv::Rect(0, lens * half, raw.cols, half)).clone();
                f.lens = lens;
                f.source = path;
                f.sequence = sequence++;
                out.frames.push_back(std::move(f));
            }
            out.model = model;
            out.description += "DNG " + path + " ";
        } else {
            for (size_t lens = 0; lens < images.size(); ++lens) {
                LensFrame f;
                f.image = images[lens];
                f.lens = static_cast<int>(lens);
                f.source = path;
                f.sequence = sequence++;
                ExifPose pose;
                if (images.size() == 1 && readExifPose(path, pose)) {
                    f.hasPose = true;
                    f.captureIndex = pose.index;
                    f.yawDegrees = pose.yawDegrees;
                    f.pitchDegrees = pose.pitchDegrees;
                    f.rollDegrees = pose.rollDegrees;
                    f.horizontalFovDegrees = pose.horizontalFovDegrees;
                    f.verticalFovDegrees = pose.verticalFovDegrees;
                    f.cameraMatrix = pose.matrix;
                    f.hasCameraMatrix = pose.hasMatrix;
                }
                out.frames.push_back(std::move(f));
            }
            out.description += "STILL " + path + " ";
        }
    }
    if (out.frames.empty()) { error = "no input frames loaded"; return false; }
    return true;
}

bool stitchAllInputs(const std::vector<std::string>& paths,
                     const StitchOptions& options,
                     std::vector<BatchItem>& out,
                     std::string& error,
                     int maxVideoFrames) {
    out.clear();
    error.clear();
    for (const auto& path : paths) {
        InputSession session;
        std::string itemError;
        if (!loadInputs({path}, session, itemError, maxVideoFrames)) {
            error += path + ": " + itemError + "\n";
            continue;
        }
        std::string diagnostics;
        const Panorama pano = stitchSession(session, options, diagnostics);
        if (pano.equirect.empty()) {
            error += path + ": " + (diagnostics.empty() ? "stitch failed" : diagnostics) + "\n";
            continue;
        }
        BatchItem item;
        item.source = path;
        item.panorama = pano;
        item.diagnostics = diagnostics;
        out.push_back(std::move(item));
    }
    return !out.empty();
}

} // namespace ps::io
