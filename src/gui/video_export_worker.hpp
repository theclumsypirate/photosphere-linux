#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <string>
#include <vector>

namespace ps::gui {

// Runs on a QThread so the GUI stays responsive during export.
class VideoExportWorker : public QObject {
    Q_OBJECT

public:
    struct Options {
        std::string input;
        std::string output;
        int width = 2048;
        double fps = 60.0;
        double everySeconds = 1.0;
        double startTime = 0.0;
        double endTime = 0.0; // 0 means end of file
        bool flipLenses = false;
        bool multiband = true;
    };

    explicit VideoExportWorker(QObject* parent = nullptr);

    void setOptions(const Options& options);
    void setCancelFlag(std::atomic<bool>* flag);

public slots:
    void run();

signals:
    void progress(int done, int total, const QString& message);
    void finished(bool ok, const QString& message);

private:
    Options options_;
    std::atomic<bool>* cancelFlag_ = nullptr;
};

} // namespace ps::gui
