#pragma once

#include <QObject>
#include <QString>

#include <opencv2/core.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

class QTimer;

namespace ps::gui {

// Sequential playback decode feeding the GPU viewer. The decoder scales each
// lens straight to the texture size, so no full-resolution frame is ever
// allocated in the GUI thread.
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
    void setSpeed(double speed) { speed_ = std::max(0.25, std::min(16.0, speed)); }

    bool playing() const { return playing_; }
    double speed() const { return speed_; }
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
        bool stop = false;
        bool finished = false;
        double offset = 0.0;
        double pendingSeek = -1.0;
        QString error;
    };

    QString path_;
    QTimer* timer_ = nullptr;
    std::shared_ptr<Queue> queue_;
    std::thread worker_;
    double fps_ = 25.0;
    double duration_ = 0.0;
    double position_ = 0.0;
    double speed_ = 1.0;
    bool playing_ = false;
    int frameCount_ = 0;
    double lastDisplayedTime_ = -1.0;
};

} // namespace ps::gui
