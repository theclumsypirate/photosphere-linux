#include "gui/hdr_export_worker.hpp"

#include "io/hdr_export.hpp"

namespace ps::gui {

HdrExportWorker::HdrExportWorker(QObject* parent) : QObject(parent) {}

void HdrExportWorker::setOptions(const Options& options) { options_ = options; }

void HdrExportWorker::run() {
    std::string error;
    const bool ok = io::exportHdrPanorama(options_.paths, options_.stitch,
                                          options_.output, error);
    emit finished(ok, ok ? QString("Saved 32-bit EXR to " +
                                   QString::fromStdString(options_.output))
                         : QString::fromStdString(error));
}

} // namespace ps::gui
