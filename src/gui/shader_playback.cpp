#include "gui/shader_playback.hpp"

#include "io/insv_reader.hpp"

#include <opencv2/imgproc.hpp>

#include <QTimer>

#include <algorithm>
#include <cmath>
#include <string>

namespace ps::gui {
namespace {

cv::Mat squareFisheye(const cv::Mat& source) {
    if (source.empty()) return {};
    const int side = std::min(source.cols, source.rows);
    const cv::Rect crop((source.cols - side) / 2, (source.rows - side) / 2,
                        side, side);
    cv::Mat result;
    cv::resize(source(crop), result, cv::Size(512, 512), 0, 0,
               cv::INTER_AREA);
    return result;
}

cv::Mat makeSideBySide(const std::vector<LensFrame>& frames) {
    if (frames.empty()) return {};
    cv::Mat left, right;
    if (frames.size() >= 2 && frames[0].lens != frames[1].lens) {
        left = frames[0].image;
        right = frames[1].image;
    } else if (frames[0].image.cols >= 2 * frames[0].image.rows) {
        const int half = frames[0].image.cols / 2;
        left = frames[0].image(cv::Rect(0, 0, half, frames[0].image.rows));
        right = frames[0].image(cv::Rect(half, 0, half, frames[0].image.rows));
    } else {
        return {};
    }
    const cv::Mat a = squareFisheye(left);
    const cv::Mat b = squareFisheye(right);
    if (a.empty() || b.empty()) return {};
    cv::Mat out;
    cv::hconcat(a, b, out);
    return out;
}

} // namespace

ShaderPlayback::ShaderPlayback(QObject* parent) : QObject(parent) {
    timer_ = new QTimer(this);
    timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, &ShaderPlayback::tick);
}

ShaderPlayback::~ShaderPlayback() {
    close();
}

bool ShaderPlayback::open(const QString& path, QString& error) {
    close();
    path_ = path;
    fps_ = io::insvFrameRate(path.toStdString());
    if (fps_ <= 0.0) fps_ = 25.0;
    duration_ = io::insvDuration(path.toStdString());
    if (duration_ <= 0.0) {
        error = "Cannot read the video duration";
        return false;
    }
    position_ = 0.0;
    lastDisplayedTime_ = -1.0;
    startDecoder(0.0);
    return true;
}

void ShaderPlayback::startDecoder(double fromSeconds) {
    stopDecoder();
    auto shared = std::make_shared<Queue>();
    queue_ = shared;
    const QString path = path_;
    const double fps = fps_;
    const double duration = duration_;
    double start = std::clamp(fromSeconds, 0.0, duration);
    shared->times.clear();
    for (double t = start; t < duration; t += 1.0 / fps)
        shared->times.push_back(t);
    if (shared->times.empty()) shared->times.push_back(start);
    worker_ = std::thread([shared, path, start]() {
        std::string error;
        const bool ok = io::streamInsvAtTimes(
            path.toStdString(), shared->times,
            [shared, start](const std::vector<LensFrame>& frames) {
                cv::Mat image = makeSideBySide(frames);
                if (image.empty()) return true;
                std::unique_lock<std::mutex> lock(shared->mutex);
                shared->cv.wait(lock, [shared]() {
                    return shared->frames.size() < 24 || shared->stop;
                });
                if (shared->stop) return false;
                const size_t index = shared->nextTime++;
                shared->frames.emplace_back(
                    index < shared->times.size() ? shared->times[index] : start,
                    image);
                shared->cv.notify_one();
                return true;
            },
            error);
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->finished = true;
            shared->cv.notify_all();
        }
        if (!ok && error.find("cancel") == std::string::npos) {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->error = QString::fromStdString(error);
        }
    });
}

void ShaderPlayback::stopDecoder() {
    if (!queue_) return;
    {
        std::lock_guard<std::mutex> lock(queue_->mutex);
        queue_->stop = true;
        queue_->cv.notify_all();
    }
    if (worker_.joinable()) worker_.join();
    queue_.reset();
}

void ShaderPlayback::close() {
    pause();
    stopDecoder();
    path_.clear();
    duration_ = 0.0;
    position_ = 0.0;
}

void ShaderPlayback::play() {
    if (path_.isEmpty() || !queue_) return;
    playing_ = true;
    timer_->start();
}

void ShaderPlayback::pause() {
    playing_ = false;
    timer_->stop();
}

void ShaderPlayback::seek(double seconds) {
    if (path_.isEmpty()) return;
    position_ = std::clamp(seconds, 0.0, duration_);
    lastDisplayedTime_ = -1.0;
    startDecoder(position_);
    emit positionChanged(position_);
}

void ShaderPlayback::tick() {
    if (!playing_) return;
    position_ = std::min(duration_, position_ + 1.0 / 60.0);
    if (!queue_) return;
    {
        std::lock_guard<std::mutex> lock(queue_->mutex);
        if (!queue_->error.isEmpty()) {
            const QString message = queue_->error;
            queue_->error.clear();
            emit failed(message);
        }
    }
    cv::Mat frame;
    double frameTime = lastDisplayedTime_;
    {
        std::lock_guard<std::mutex> lock(queue_->mutex);
        while (!queue_->frames.empty() &&
               queue_->frames.front().first <= position_) {
            frameTime = queue_->frames.front().first;
            frame = queue_->frames.front().second;
            queue_->frames.pop_front();
            queue_->cv.notify_one();
        }
    }
    if (!frame.empty() && frameTime > lastDisplayedTime_) {
        lastDisplayedTime_ = frameTime;
        emit frameReady(frame);
    }
    emit positionChanged(position_);
    if (position_ >= duration_) pause();
}

} // namespace ps::gui
