#include "gui/multi_camera_dialog.hpp"

#include "core/multi_view.hpp"

#include <opencv2/imgcodecs.hpp>

#include <QCheckBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace ps::gui {

MultiCameraDialog::MultiCameraDialog(const cv::Mat& panorama, QWidget* parent)
    : QDialog(parent), panorama_(panorama) {
    setWindowTitle("Multi-camera view");
    resize(1000, 700);
    previewLabel_ = new QLabel(this);
    previewLabel_->setMinimumSize(400, 225);
    previewLabel_->setAlignment(Qt::AlignCenter);

    auto makeSlider = [this](int min, int max, int value) {
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(min, max);
        slider->setValue(value);
        connect(slider, &QSlider::valueChanged, this, [this]() {
            if (!previewTimer_) {
                previewTimer_ = new QTimer(this);
                previewTimer_->setSingleShot(true);
                connect(previewTimer_, &QTimer::timeout, this,
                        &MultiCameraDialog::preview);
            }
            previewTimer_->start(150);
        });
        return slider;
    };
    yawA_ = makeSlider(-180, 180, 0);
    pitchA_ = makeSlider(-90, 90, 0);
    fovA_ = makeSlider(20, 120, 75);
    yawB_ = makeSlider(-180, 180, 180);
    pitchB_ = makeSlider(-90, 90, 0);
    fovB_ = makeSlider(20, 120, 75);

    auto* controls = new QVBoxLayout();
    auto addLabel = [controls, this](const char* text) {
        controls->addWidget(new QLabel(text, this));
    };
    addLabel("Camera A: yaw"); controls->addWidget(yawA_);
    addLabel("Camera A: pitch"); controls->addWidget(pitchA_);
    addLabel("Camera A: FOV"); controls->addWidget(fovA_);
    addLabel("Camera B: yaw"); controls->addWidget(yawB_);
    addLabel("Camera B: pitch"); controls->addWidget(pitchB_);
    addLabel("Camera B: FOV"); controls->addWidget(fovB_);
    pip_ = new QCheckBox("Picture in picture", this);
    pip_->setChecked(true);
    connect(pip_, &QCheckBox::toggled, this, [this]() { preview(); });
    controls->addWidget(pip_);
    auto* saveButton = new QPushButton("Save image", this);
    controls->addWidget(saveButton);
    controls->addStretch(1);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(previewLabel_, 1);
    layout->addLayout(controls);
    connect(saveButton, &QPushButton::clicked, this, &MultiCameraDialog::save);
    preview();
}

void MultiCameraDialog::preview() {
    VirtualCamera a{yawA_->value(), pitchA_->value(), fovA_->value()};
    VirtualCamera b{yawB_->value(), pitchB_->value(), fovB_->value()};
    const cv::Mat image = pip_->isChecked()
                              ? renderPipView(panorama_, a, b, 960, 480)
                              : renderSplitView(panorama_, a, b, 960, 480);
    if (image.empty()) return;
    previewImage_ = QImage(image.data, image.cols, image.rows,
                           static_cast<qsizetype>(image.step), QImage::Format_BGR888).copy();
    previewLabel_->setPixmap(QPixmap::fromImage(previewImage_)
                                 .scaled(960, 480, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
}

void MultiCameraDialog::save() {
    const QString path = QFileDialog::getSaveFileName(this, "Save multi-camera image",
                                                      QString(), "PNG (*.png);;JPEG (*.jpg)");
    if (path.isEmpty()) return;
    VirtualCamera a{yawA_->value(), pitchA_->value(), fovA_->value()};
    VirtualCamera b{yawB_->value(), pitchB_->value(), fovB_->value()};
    const cv::Mat image = pip_->isChecked()
                              ? renderPipView(panorama_, a, b, 1920, 1080)
                              : renderSplitView(panorama_, a, b, 1920, 1080);
    if (cv::imwrite(path.toStdString(), image))
        QMessageBox::information(this, "Saved", path);
    else
        QMessageBox::warning(this, "Save failed", "Could not write the image.");
}

} // namespace ps::gui
