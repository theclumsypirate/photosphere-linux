#include "gui/shader_playback.hpp"

#include "io/insv_reader.hpp"

#include <opencv2/imgproc.hpp>

#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ps::gui {
namespace {

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
    if (left.empty() || right.empty()) return {};
    cv::Mat out;
    cv::hconcat(left, right, out);
    return out;
}

} // namespace

ShaderPlayback::ShaderPlayback(QObject* parent) : QObject(parent) {
    timer_ = new QTimer(this);
    timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, &ShaderPlayback::tick);
}

ShaderPlayback::~ShaderPlayback() { close(); }

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
    frameCount_ = 0;
    startDecoder(0.0);
    timer_->start();
    return true;
}

void ShaderPlayback::startDecoder(double fromSeconds) {
    stopDecoder();
    auto shared = std::make_shared<Queue>();
    shared->offset = std::clamp(fromSeconds, 0.0, duration_);
    queue_ = shared;
    const QString path = path_;
    double start = shared->offset;
    auto seekRequest = [shared]() -> double {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->pendingSeek < 0.0) return -1.0;
        const double target = shared->pendingSeek;
        shared->pendingSeek = -1.0;
        shared->offset = target;
        return target;
    };
    worker_ = std::thread([shared, path, start, seekRequest]() {
        std::string error;
        const bool ok = io::streamInsvSequential(
            path.toStdString(), start,
            [shared](const std::vector<LensFrame>& frames) {
                cv::Mat image = makeSideBySide(frames);
                if (image.empty()) return true;
                std::lock_guard<std::mutex> lock(shared->mutex);
                if (shared->stop) return false;
                // Keep the freshest frames only, so playback never lags.
                if (shared->frames.size() >= 4) shared->frames.pop_front();
                shared->frames.emplace_back(frames[0].timestamp - shared->offset,
                                            image);
                return true;
            },
            error, 512, seekRequest);
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->finished = true;
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
    }
    if (worker_.joinable()) worker_.join();
    queue_.reset();
}

void ShaderPlayback::close() {
    playing_ = false;
    timer_->stop();
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

void ShaderPlayback::pause() { playing_ = false; }

void ShaderPlayback::seek(double seconds) {
    if (path_.isEmpty() || !queue_) return;
    position_ = std::clamp(seconds, 0.0, duration_);
    lastDisplayedTime_ = -1.0;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(queue_->mutex);
        finished = queue_->finished;
    }
    if (finished) {
        // Nothing is reading packets any more, so a new decoder is required.
        startDecoder(position_);
    } else {
        std::lock_guard<std::mutex> lock(queue_->mutex);
        queue_->frames.clear();
        queue_->pendingSeek = position_;
    }
    emit positionChanged(position_);
}

void ShaderPlayback::tick() {
    if (playing_) position_ = std::min(duration_, position_ + speed_ / 60.0);
    if (!queue_) return;
    cv::Mat frame;
    double frameTime = lastDisplayedTime_;
    {
        std::lock_guard<std::mutex> lock(queue_->mutex);
        if (!queue_->error.isEmpty()) {
            const QString message = queue_->error;
            queue_->error.clear();
            emit failed(message);
        }
        while (!queue_->frames.empty() &&
               queue_->frames.front().first <= position_ + 0.05) {
            frameTime = queue_->frames.front().first;
            frame = queue_->frames.front().second;
            queue_->frames.pop_front();
        }
    }
    if (!frame.empty()) {
        lastDisplayedTime_ = frameTime;
        if (frameCount_++ == 0)
            std::fprintf(stderr, "GPU preview: first frame %dx%d\n",
                         frame.cols, frame.rows);
        emit frameReady(frame);
    }
    emit positionChanged(position_);
    if (position_ >= duration_) pause();
}

} // namespace ps::gui
