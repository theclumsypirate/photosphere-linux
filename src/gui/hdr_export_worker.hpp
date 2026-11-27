#pragma once

#include <QObject>
#include <QString>

#include "core/stitcher.hpp"

#include <string>
#include <vector>

namespace ps::gui {

class HdrExportWorker : public QObject {
    Q_OBJECT

public:
    struct Options {
        std::vector<std::string> paths;
        StitchOptions stitch;
        std::string output;
    };

    explicit HdrExportWorker(QObject* parent = nullptr);
    void setOptions(const Options& options);

private:
    Options options_;

public slots:
    void run();

signals:
    void finished(bool ok, const QString& message);
};

} // namespace ps::gui
