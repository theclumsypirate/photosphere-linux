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
    for (size_t i = 0; i < times.size(); ++i) {
        if (cancelled()) break;
        std::vector<LensFrame> frames;
        if (!io::readInsvAtTimes(options_.input, std::vector<double>{times[i]},
                                 frames, error)) continue;
        if (frames.size() < 2 || frames[0].lens == frames[1].lens) continue;
        std::string diagnostics;
        const Panorama pano = stitchDualFisheye({frames[0], frames[1]},
                                                stitchOptions, diagnostics);
        if (pano.equirect.empty()) continue;
        if (!writer.write(pano.equirect, error)) break;
        ++written;
        if ((i % 2) == 0 || i + 1 == times.size())
            emit progress(static_cast<int>(i + 1), static_cast<int>(times.size()),
                          QString("Encoding frame %1 of %2").arg(i + 1).arg(times.size()));
    }
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
