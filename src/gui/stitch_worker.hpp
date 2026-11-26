#pragma once

#include <QObject>
#include <QString>

#include "core/stitcher.hpp"
#include "core/types.hpp"

#include <string>
#include <vector>

namespace ps::gui {

class StitchWorker : public QObject {
    Q_OBJECT

public:
    struct Options {
        std::vector<std::string> paths;
        StitchOptions stitch;
        int maxVideoFrames = 1;
    };

    explicit StitchWorker(QObject* parent = nullptr);

    void setOptions(const Options& options);
    const Panorama& result() const { return result_; }
    const std::string& diagnostics() const { return diagnostics_; }

public slots:
    void run();

signals:
    void progress(int done, int total, const QString& message);
    void finished(bool ok, const QString& message);

private:
    Options options_;
    Panorama result_;
    std::string diagnostics_;
};

} // namespace ps::gui
