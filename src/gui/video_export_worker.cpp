#include "gui/video_export_worker.hpp"

#include "core/stitcher.hpp"
#include "io/insv_reader.hpp"
#include "io/video_writer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ps::gui {

VideoExportWorker::VideoExportWorker(QObject* parent) : QObject(parent) {}

void VideoExportWorker::setOptions(const Options& options) { options_ = options; }

void VideoExportWorker::setCancelFlag(std::atomic<bool>* flag) { cancelFlag_ = flag; }

void VideoExportWorker::run() {
    auto cancelled = [this]() { return cancelFlag_ && cancelFlag_->load(); };
    int width = options_.width;
    if (width & 1) --width;
    if (width < 256) width = 256;

    StitchOptions stitchOptions;
    stitchOptions.width = width;
    stitchOptions.multiband = options_.multiband;
    stitchOptions.flipLenses = options_.flipLenses;
    stitchOptions.dropOutliers = false;
    stitchOptions.useLocalWarp = false;
    stitchOptions.useSeams = false;

    std::string error;
    const double duration = io::insvDuration(options_.input);
    const double end = options_.endTime > 0
                           ? std::min(options_.endTime, duration)
                           : duration;
    std::vector<double> times;
    for (double t = options_.startTime; t < end; t += options_.everySeconds)
        times.push_back(t);
    if (times.empty()) times.push_back(options_.startTime);

    io::Mp4Writer writer(options_.output, width, width / 2, options_.fps);
    writer.setVideoQuality(18.0, 0);
    writer.setHevc(options_.hevc);
    writer.setSphericalMetadata(options_.spherical);
    const double videoEnd = options_.startTime +
                            static_cast<double>(times.size()) * options_.everySeconds;
    writer.setAudioSource(options_.input, options_.startTime,
                          options_.endTime > 0 ? std::min(options_.endTime, videoEnd)
                                               : videoEnd);
    if (!writer.open(error)) {
        emit finished(false, QString::fromStdString(error));
        return;
    }

    int written = 0;
    const size_t total = times.size();
    auto onFrames = [&](const std::vector<LensFrame>& frames) -> bool {
        if (cancelled()) return false;
        if (frames.size() < 2 || frames[0].lens == frames[1].lens) return true;
        std::string diagnostics;
        const Panorama pano = stitchDualFisheye({frames[0], frames[1]},
                                                stitchOptions, diagnostics);
        if (pano.equirect.empty()) return true;
        if (!writer.write(pano.equirect, error)) return false;
        ++written;
        if ((written % 2) == 1 || written == static_cast<int>(total))
            emit progress(written, static_cast<int>(total),
                          QString("Encoding frame %1 of %2").arg(written).arg(total));
        return true;
    };
    io::streamInsvAtTimes(options_.input, times, onFrames, error);
    writer.finish();
    if (cancelled()) {
        emit finished(false, "Export cancelled");
    } else if (written == 0) {
        emit finished(false, error.empty() ? "No panorama frames were produced" :
                                             QString::fromStdString(error));
    } else {
        emit finished(true, QString("Exported %1 panorama frames to %2")
                                .arg(written)
                                .arg(QString::fromStdString(options_.output)));
    }
}

} // namespace ps::gui
