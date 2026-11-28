#include "gui/multi_camera_dialog.hpp"

#include "core/little_planet.hpp"
#include "core/multi_view.hpp"
#include "core/stitcher.hpp"
#include "io/frame_store.hpp"
#include "io/insv_reader.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <string>

namespace ps::gui {

MultiCameraDialog::MultiCameraDialog(const cv::Mat& panorama, QWidget* parent)
    : QDialog(parent) {
    panorama_[0] = panorama;
    camera_[1].yaw = 180.0;
    build();
}

MultiCameraDialog::MultiCameraDialog(const cv::Mat& first, const cv::Mat& second,
                                     QWidget* parent)
    : QDialog(parent) {
    panorama_[0] = first;
    panorama_[1] = second;
    camera_[1].yaw = 180.0;
    build();
}

MultiCameraDialog::MultiCameraDialog(const QString& videoPath, QWidget* parent)
    : QDialog(parent), videoPath_(videoPath) {
    videoDuration_ = io::insvDuration(videoPath.toStdString());
    build();
    if (videoDuration_ > 0.0) {
        time_->setEnabled(true);
        capture(0);
        capture(1);
    }
}

void MultiCameraDialog::schedulePreview() {
    if (!previewTimer_) {
        previewTimer_ = new QTimer(this);
        previewTimer_->setSingleShot(true);
        connect(previewTimer_, &QTimer::timeout, this,
                &MultiCameraDialog::preview);
    }
    previewTimer_->start(60);
}

void MultiCameraDialog::build() {
    setWindowTitle("Multi-camera view");
    resize(1100, 720);

    previewLabel_ = new QLabel(this);
    previewLabel_->setMinimumSize(480, 270);
    previewLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_ = new QLabel("Drag on the image to aim the selected camera.",
                              this);

    editFirst_ = new QRadioButton("Edit panorama 1", this);
    editSecond_ = new QRadioButton("Edit panorama 2", this);
    editFirst_->setChecked(true);
    connect(editFirst_, &QRadioButton::toggled, this, [this](bool on) {
        if (on) { selected_ = 0; fov_->setValue(static_cast<int>(camera_[0].fov)); }
    });
    connect(editSecond_, &QRadioButton::toggled, this, [this](bool on) {
        if (on) { selected_ = 1; fov_->setValue(static_cast<int>(camera_[1].fov)); }
    });

    fov_ = new QSlider(Qt::Horizontal, this);
    fov_->setRange(20, 120);
    fov_->setValue(static_cast<int>(camera_[0].fov));
    connect(fov_, &QSlider::valueChanged, this, [this](int value) {
        camera_[selected_].fov = value;
        schedulePreview();
    });

    pip_ = new QCheckBox("Picture in picture", this);
    pip_->setChecked(true);
    connect(pip_, &QCheckBox::toggled, this, [this]() { preview(); });

    auto* side = new QVBoxLayout();
    side->addWidget(new QLabel("Edit", this));
    side->addWidget(editFirst_);
    side->addWidget(editSecond_);
    side->addWidget(new QLabel("Field of view", this));
    side->addWidget(fov_);
    side->addWidget(pip_);

    if (!videoPath_.isEmpty()) {
        time_ = new QSlider(Qt::Horizontal, this);
        time_->setRange(0, 1000);
        time_->setEnabled(false);
        connect(time_, &QSlider::valueChanged, this,
                &MultiCameraDialog::onTimeChanged);
        auto* captureFirst = new QPushButton("Capture panorama 1 here", this);
        auto* captureSecond = new QPushButton("Capture panorama 2 here", this);
        connect(captureFirst, &QPushButton::clicked, this, [this]() { capture(0); });
        connect(captureSecond, &QPushButton::clicked, this, [this]() { capture(1); });
        side->addWidget(new QLabel("Video time", this));
        side->addWidget(time_);
        side->addWidget(captureFirst);
        side->addWidget(captureSecond);
    }

    auto* saveButton = new QPushButton("Save image", this);
    connect(saveButton, &QPushButton::clicked, this, &MultiCameraDialog::save);
    side->addWidget(saveButton);
    side->addStretch(1);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(previewLabel_, 1);
    layout->addLayout(side);
    auto* bottom = new QVBoxLayout();
    bottom->addLayout(layout);
    bottom->addWidget(statusLabel_);
    setLayout(bottom);

    preview();
}

cv::Mat MultiCameraDialog::currentPanorama() const {
    return !panorama_[selected_].empty() ? panorama_[selected_]
                                         : panorama_[0];
}

void MultiCameraDialog::preview() {
    const cv::Mat& main = !panorama_[0].empty() ? panorama_[0] : panorama_[1];
    const cv::Mat& secondary = !panorama_[1].empty() ? panorama_[1] : panorama_[0];
    if (main.empty()) return;
    const cv::Mat image = pip_->isChecked()
                              ? renderPipView(main, camera_[0], secondary,
                                              camera_[1], 1024, 512)
                              : renderSplitView(main, camera_[0], secondary,
                                                camera_[1], 800, 400);
    if (image.empty()) return;
    previewImage_ = QImage(image.data, image.cols, image.rows,
                           static_cast<qsizetype>(image.step),
                           QImage::Format_BGR888).copy();
    previewLabel_->setPixmap(QPixmap::fromImage(previewImage_));
    statusLabel_->setText(QString("Editing panorama %1 - drag to aim, wheel not used here")
                              .arg(selected_ + 1));
}

void MultiCameraDialog::capture(int index) {
    if (videoPath_.isEmpty()) return;
    const double at = time_ ? time_->value() / 1000.0 * videoDuration_ : 0.0;
    std::vector<LensFrame> frames;
    std::string error;
    if (!io::readInsvAtTimes(videoPath_.toStdString(), {at}, frames, error)) {
        statusLabel_->setText(QString::fromStdString(error));
        return;
    }
    ps::StitchOptions options;
    options.width = 4096;
    options.multiband = false;
    options.useLocalWarp = false;
    options.useSeams = false;
    // Build a small input session from this one frame pair.
    ps::io::InputSession session;
    session.frames = frames;
    const ps::Panorama result = ps::stitchSession(session, options, error);
    if (result.equirect.empty()) {
        statusLabel_->setText(QString::fromStdString(error));
        return;
    }
    panorama_[index] = result.equirect.clone();
    statusLabel_->setText(QString("Captured panorama %1 at %2 s")
                              .arg(index + 1).arg(at, 0, 'f', 1));
    preview();
}

void MultiCameraDialog::onTimeChanged(int value) {
    if (!time_ || videoDuration_ <= 0.0) return;
    statusLabel_->setText(QString("Time %1 s - press Capture to take a panorama")
                              .arg(value / 1000.0 * videoDuration_, 0, 'f', 1));
}

void MultiCameraDialog::save() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Save multi-camera image", QString(), "PNG (*.png);;JPEG (*.jpg)");
    if (path.isEmpty()) return;
    const cv::Mat& main = !panorama_[0].empty() ? panorama_[0] : panorama_[1];
    const cv::Mat& secondary = !panorama_[1].empty() ? panorama_[1] : panorama_[0];
    const cv::Mat image = pip_->isChecked()
                              ? renderPipView(main, camera_[0], secondary,
                                              camera_[1], 2048, 1024)
                              : renderSplitView(main, camera_[0], secondary,
                                                camera_[1], 2048, 1024);
    if (!image.empty() && cv::imwrite(path.toStdString(), image))
        QMessageBox::information(this, "Saved", path);
    else
        QMessageBox::warning(this, "Save failed", "Could not write the image.");
}

void MultiCameraDialog::mousePressEvent(QMouseEvent* event) {
    lastPos_ = event->pos();
    QDialog::mousePressEvent(event);
}

void MultiCameraDialog::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton && previewLabel_->geometry().contains(event->pos())) {
        const QPoint delta = event->pos() - lastPos_;
        camera_[selected_].yaw -= delta.x() * 0.35;
        camera_[selected_].pitch = std::clamp(camera_[selected_].pitch +
                                              delta.y() * 0.35, -89.0, 89.0);
        lastPos_ = event->pos();
        schedulePreview();
    }
    QDialog::mouseMoveEvent(event);
}

} // namespace ps::gui
