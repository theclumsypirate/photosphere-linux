#include "gui/multi_camera_dialog.hpp"

#include "core/multi_view.hpp"

#include <opencv2/imgcodecs.hpp>

#include <QCheckBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace ps::gui {

MultiCameraDialog::MultiCameraDialog(const cv::Mat& panorama, QWidget* parent)
    : QDialog(parent) {
    panorama_[0] = panorama;
    camera_[1].yaw = 180.0;
    build();
    restoreState();
}

MultiCameraDialog::MultiCameraDialog(const cv::Mat& first,
                                     const cv::Mat& second, QWidget* parent)
    : QDialog(parent) {
    panorama_[0] = first;
    panorama_[1] = second;
    camera_[1].yaw = 180.0;
    build();
    restoreState();
}

MultiCameraDialog::~MultiCameraDialog() = default;

void MultiCameraDialog::saveState() {
    QSettings settings("Photosphere", "multiCamera");
    for (int i = 0; i < 2; ++i) {
        settings.setValue(QString("cam%1/yaw").arg(i), camera_[i].yaw);
        settings.setValue(QString("cam%1/pitch").arg(i), camera_[i].pitch);
        settings.setValue(QString("cam%1/fov").arg(i), camera_[i].fov);
    }
    settings.setValue("selected", selected_);
    settings.setValue("pip", pip_->isChecked());
}

void MultiCameraDialog::restoreState() {
    QSettings settings("Photosphere", "multiCamera");
    for (int i = 0; i < 2; ++i) {
        camera_[i].yaw = settings.value(QString("cam%1/yaw").arg(i),
                                        camera_[i].yaw).toDouble();
        camera_[i].pitch = settings.value(QString("cam%1/pitch").arg(i),
                                          camera_[i].pitch).toDouble();
        camera_[i].fov = settings.value(QString("cam%1/fov").arg(i),
                                        camera_[i].fov).toDouble();
    }
    selected_ = settings.value("selected", 0).toInt();
    if (selected_ == 1) editSecond_->setChecked(true);
    fov_->setValue(static_cast<int>(camera_[selected_].fov));
    pip_->setChecked(settings.value("pip", true).toBool());
}

void MultiCameraDialog::closeEvent(QCloseEvent* event) {
    saveState();
    QDialog::closeEvent(event);
}

void MultiCameraDialog::schedulePreview() {
    if (!previewTimer_) {
        previewTimer_ = new QTimer(this);
        previewTimer_->setSingleShot(true);
        connect(previewTimer_, &QTimer::timeout, this,
                &MultiCameraDialog::preview);
    }
    previewTimer_->start(16);
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
    fov_->setRange(15, 120);
    fov_->setValue(static_cast<int>(camera_[0].fov));
    connect(fov_, &QSlider::valueChanged, this, [this](int value) {
        camera_[selected_].fov = value;
        schedulePreview();
    });

    pip_ = new QCheckBox("Picture in picture", this);
    pip_->setChecked(true);
    connect(pip_, &QCheckBox::toggled, this, [this]() { preview(); });

    auto* loadFirst = new QPushButton("Load picture 1", this);
    auto* loadSecond = new QPushButton("Load picture 2", this);
    connect(loadFirst, &QPushButton::clicked, this, [this]() { loadPicture(0); });
    connect(loadSecond, &QPushButton::clicked, this, [this]() { loadPicture(1); });
    auto* saveButton = new QPushButton("Save image", this);
    connect(saveButton, &QPushButton::clicked, this, &MultiCameraDialog::save);

    auto* side = new QVBoxLayout();
    side->addWidget(new QLabel("Edit", this));
    side->addWidget(editFirst_);
    side->addWidget(editSecond_);
    side->addWidget(new QLabel("Field of view", this));
    side->addWidget(fov_);
    side->addWidget(pip_);
    side->addWidget(new QLabel("Pictures", this));
    side->addWidget(loadFirst);
    side->addWidget(loadSecond);
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
    return !panorama_[selected_].empty() ? panorama_[selected_] : panorama_[0];
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
    statusLabel_->setText(QString("Editing panorama %1 - drag to aim, wheel to zoom")
                              .arg(selected_ + 1));
}

void MultiCameraDialog::loadPicture(int index) {
    const QString path = QFileDialog::getOpenFileName(
        this, "Choose panorama " + QString::number(index + 1), QString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;All files (*)");
    if (path.isEmpty()) return;
    const cv::Mat image = cv::imread(path.toStdString(), cv::IMREAD_COLOR);
    if (image.empty()) {
        QMessageBox::warning(this, "Load failed", "Cannot read " + path);
        return;
    }
    panorama_[index] = image;
    preview();
}

void MultiCameraDialog::save() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Save multi-camera image", QString(), "PNG (*.png);;JPEG (*.jpg)");
    if (path.isEmpty()) return;
    if (!previewImage_.isNull() && previewImage_.save(path))
        QMessageBox::information(this, "Saved", path);
    else
        QMessageBox::warning(this, "Save failed", "Could not write the image.");
}

void MultiCameraDialog::mousePressEvent(QMouseEvent* event) {
    lastPos_ = event->pos();
    QDialog::mousePressEvent(event);
}

void MultiCameraDialog::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton &&
        previewLabel_->geometry().contains(event->pos())) {
        const QPoint delta = event->pos() - lastPos_;
        camera_[selected_].yaw -= delta.x() * 0.3;
        camera_[selected_].pitch = std::clamp(camera_[selected_].pitch -
                                              delta.y() * 0.3, -89.0, 89.0);
        lastPos_ = event->pos();
        schedulePreview();
    }
    QDialog::mouseMoveEvent(event);
}

void MultiCameraDialog::wheelEvent(QWheelEvent* event) {
    const int steps = event->angleDelta().y() / 120;
    camera_[selected_].fov = std::clamp(camera_[selected_].fov + steps * 5.0,
                                        15.0, 120.0);
    fov_->setValue(static_cast<int>(camera_[selected_].fov));
    schedulePreview();
}

} // namespace ps::gui
