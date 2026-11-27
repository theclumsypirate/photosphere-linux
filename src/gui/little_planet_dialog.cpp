#include "gui/little_planet_dialog.hpp"

#include "core/little_planet.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

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

LittlePlanetDialog::LittlePlanetDialog(const cv::Mat& panorama, QWidget* parent)
    : QDialog(parent), panorama_(panorama) {
    setWindowTitle("Little planet");
    resize(900, 760);

    previewLabel_ = new QLabel(this);
    previewLabel_->setMinimumSize(360, 360);
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
                        &LittlePlanetDialog::preview);
            }
            previewTimer_->start(120);
        });
        return slider;
    };
    fov_ = makeSlider(20, 350, 180);
    tilt_ = makeSlider(-180, 180, 0);
    pan_ = makeSlider(-180, 180, 0);
    spin_ = makeSlider(-180, 180, 0);

    auto* sliders = new QVBoxLayout();
    sliders->addWidget(new QLabel("Field of view", this));
    sliders->addWidget(fov_);
    sliders->addWidget(new QLabel("Tilt", this));
    sliders->addWidget(tilt_);
    sliders->addWidget(new QLabel("Pan", this));
    sliders->addWidget(pan_);
    sliders->addWidget(new QLabel("Spin", this));
    sliders->addWidget(spin_);
    saveButton_ = new QPushButton("Save PNG", this);
    sliders->addWidget(saveButton_);
    sliders->addStretch(1);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(previewLabel_, 1);
    layout->addLayout(sliders);

    connect(saveButton_, &QPushButton::clicked, this, &LittlePlanetDialog::save);
    preview();
}

void LittlePlanetDialog::preview() {
    LittlePlanetOptions options;
    options.outputSize = 512;
    options.fovDegrees = fov_->value();
    options.tiltDegrees = tilt_->value();
    options.panDegrees = pan_->value();
    options.spinDegrees = spin_->value();
    const cv::Mat image = convertToLittlePlanet(panorama_, options);
    if (image.empty()) return;
    cv::Mat bgr;
    cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
    previewImage_ = QImage(bgr.data, bgr.cols, bgr.rows,
                           static_cast<qsizetype>(bgr.step), QImage::Format_BGR888).copy();
    previewLabel_->setPixmap(QPixmap::fromImage(previewImage_)
                                 .scaled(640, 640, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
}

void LittlePlanetDialog::save() {
    const QString path = QFileDialog::getSaveFileName(this, "Save little planet",
                                                      QString(), "PNG (*.png)");
    if (path.isEmpty()) return;
    LittlePlanetOptions options;
    options.outputSize = std::min(4096, panorama_.cols / 2);
    options.fovDegrees = fov_->value();
    options.tiltDegrees = tilt_->value();
    options.panDegrees = pan_->value();
    options.spinDegrees = spin_->value();
    const cv::Mat image = convertToLittlePlanet(panorama_, options);
    if (cv::imwrite(path.toStdString(), image))
        QMessageBox::information(this, "Saved", path);
    else
        QMessageBox::warning(this, "Save failed", "Could not write the PNG.");
}

} // namespace ps::gui
