#include "gui/stitch_worker.hpp"

#include "io/frame_store.hpp"

#include <algorithm>

namespace ps::gui {

StitchWorker::StitchWorker(QObject* parent) : QObject(parent) {}

void StitchWorker::setOptions(const Options& options) { options_ = options; }

void StitchWorker::run() {
    diagnostics_.clear();
    emit progress(1, 10, "Loading inputs...");
    io::InputSession session;
    if (!io::loadInputs(options_.paths, session, diagnostics_,
                        options_.maxVideoFrames)) {
        emit finished(false, QString::fromStdString(diagnostics_));
        return;
    }
    emit progress(3, 10, QString("Stitching %1 lens frames...")
                             .arg(static_cast<int>(session.frames.size())));

    StitchOptions options = options_.stitch;
    const int levels = 6;
    options.onProgress = [this, levels](int level, int total) {
        const int step = std::max(1, total);
        const int value = 3 + (level * 6) / step;
        emit progress(std::min(9, value), 10,
                      QString("Rendering pyramid level %1 of %2").arg(level).arg(total));
    };
    try {
        result_ = stitchSession(session, options, diagnostics_);
    } catch (const std::exception& e) {
        diagnostics_ = std::string("exception: ") + e.what();
    }
    if (result_.equirect.empty()) {
        emit finished(false, diagnostics_.empty() ? "Stitch failed"
                                                 : QString::fromStdString(diagnostics_));
        return;
    }
    emit progress(10, 10, "Done");
    emit finished(true, QString::fromStdString(diagnostics_));
}

} // namespace ps::gui
