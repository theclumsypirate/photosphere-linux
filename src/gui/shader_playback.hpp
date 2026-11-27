#pragma once

#include <QObject>
#include <QString>

#include <opencv2/core.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

class QTimer;

namespace ps::gui {

// Decodes a video in a background thread and feeds the GPU viewer at up to
// 60 frames per second. A small bounded queue keeps memory usage low.
class ShaderPlayback : public QObject {
    Q_OBJECT

public:
    explicit ShaderPlayback(QObject* parent = nullptr);
    ~ShaderPlayback() override;

    bool open(const QString& path, QString& error);
    void play();
    void pause();
    void seek(double seconds);
    void close();

    bool playing() const { return playing_; }
    double position() const { return position_; }
    double duration() const { return duration_; }

signals:
    void frameReady(const cv::Mat& frame);
    void positionChanged(double seconds);
    void failed(const QString& message);

private slots:
    void tick();

private:
    void startDecoder(double fromSeconds);
    void stopDecoder();

    struct Queue {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::pair<double, cv::Mat>> frames;
        std::vector<double> times;
        size_t nextTime = 0;
        bool stop = false;
        bool finished = false;
        QString error;
    };

    QString path_;
    QTimer* timer_ = nullptr;
    std::shared_ptr<Queue> queue_;
    std::thread worker_;
    double fps_ = 25.0;
    double duration_ = 0.0;
    double position_ = 0.0;
    bool playing_ = false;
    double lastDisplayedTime_ = -1.0;
};

} // namespace ps::gui
