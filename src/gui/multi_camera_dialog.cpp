#include "gui/multi_camera_dialog.hpp"

#include "gui/viewer_widget.hpp"

#include <opencv2/imgcodecs.hpp>

#include <QCheckBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>

namespace ps::gui {

MultiCameraDialog::MultiCameraDialog(const cv::Mat& panorama, QWidget* parent)
    : QDialog(parent) {
    panorama_[0] = panorama;
    build();
    restoreState();
}

MultiCameraDialog::MultiCameraDialog(const cv::Mat& first,
                                     const cv::Mat& second, QWidget* parent)
    : QDialog(parent) {
    panorama_[0] = first;
    panorama_[1] = second;
    build();
    restoreState();
}

MultiCameraDialog::~MultiCameraDialog() = default;

void MultiCameraDialog::build() {
    setWindowTitle("Multi-camera view");
    resize(1100, 720);
    setMinimumSize(700, 500);

    stage_ = new QWidget(this);
    view_ = new ViewerWidget(stage_);
    view_->setOverlayButtonsVisible(false);
    view_->setPanorama(!panorama_[0].empty() ? panorama_[0] : panorama_[1]);
    view_->setSecondaryPanorama(!panorama_[1].empty() ? panorama_[1]
                                                      : panorama_[0]);

    auto* loadFirst = new QPushButton("Load picture 1", this);
    auto* loadSecond = new QPushButton("Load picture 2", this);
    connect(loadFirst, &QPushButton::clicked, this, [this]() { loadPicture(0); });
    connect(loadSecond, &QPushButton::clicked, this, [this]() { loadPicture(1); });
    pip_ = new QCheckBox("Picture in picture", this);
    pip_->setChecked(true);
    connect(pip_, &QCheckBox::toggled, this,
            [this](bool on) { view_->setPipEnabled(on); });
    auto* saveButton = new QPushButton("Save image", this);
    connect(saveButton, &QPushButton::clicked, this, &MultiCameraDialog::save);

    auto* side = new QVBoxLayout();
    side->addWidget(new QLabel("Pictures", this));
    side->addWidget(loadFirst);
    side->addWidget(loadSecond);
    side->addWidget(pip_);
    side->addWidget(saveButton);
    side->addStretch(1);

    auto* main = new QHBoxLayout(this);
    main->addWidget(stage_, 1);
    main->addLayout(side);
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
    if (index == 0)
        view_->setPanorama(image);
    else
        view_->setSecondaryPanorama(image);
}

void MultiCameraDialog::save() {
    const QImage image = view_->currentView().toImage();
    if (image.isNull()) {
        QMessageBox::warning(this, "Save failed", "Nothing to save.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Save multi-camera image", QString(), "PNG (*.png);;JPEG (*.jpg)");
    if (path.isEmpty()) return;
    if (!image.save(path))
        QMessageBox::warning(this, "Save failed", "Could not write " + path);
}

void MultiCameraDialog::saveState() {
    QSettings settings("Photosphere", "multiCamera");
    settings.setValue("cam0/yaw", view_->yaw());
    settings.setValue("cam0/pitch", view_->pitch());
    settings.setValue("cam0/fov", view_->fov());
    settings.setValue("cam1/yaw", view_->yaw2());
    settings.setValue("cam1/pitch", view_->pitch2());
    settings.setValue("cam1/fov", view_->fov2());
    settings.setValue("pip", pip_->isChecked());
}

void MultiCameraDialog::restoreState() {
    QSettings settings("Photosphere", "multiCamera");
    view_->setView(settings.value("cam0/yaw", 0.0).toDouble(),
                   settings.value("cam0/pitch", 0.0).toDouble(),
                   settings.value("cam0/fov", 75.0).toDouble());
    view_->setSecondaryView(settings.value("cam1/yaw", 180.0).toDouble(),
                            settings.value("cam1/pitch", 0.0).toDouble(),
                            settings.value("cam1/fov", 75.0).toDouble());
    pip_->setChecked(settings.value("pip", true).toBool());
    view_->setPipEnabled(pip_->isChecked());
}

void MultiCameraDialog::closeEvent(QCloseEvent* event) {
    saveState();
    QDialog::closeEvent(event);
}

} // namespace ps::gui
