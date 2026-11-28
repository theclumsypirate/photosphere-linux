#include "gui/multi_camera_dialog.hpp"

#include "gui/viewer_widget.hpp"

#include <opencv2/imgcodecs.hpp>

#include <QCheckBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
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
    stage_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    for (int i = 0; i < 2; ++i) {
        view_[i] = new ViewerWidget(stage_);
        view_[i]->setOverlayButtonsVisible(false);
        view_[i]->setPanorama(!panorama_[i].empty() ? panorama_[i]
                                                    : panorama_[0]);
        view_[i]->setView(i == 1 ? 180.0 : 0.0, 0.0, 75.0);
    }
    view_[1]->raise();

    auto* loadFirst = new QPushButton("Load picture 1", this);
    auto* loadSecond = new QPushButton("Load picture 2", this);
    connect(loadFirst, &QPushButton::clicked, this, [this]() { loadPicture(0); });
    connect(loadSecond, &QPushButton::clicked, this, [this]() { loadPicture(1); });
    pip_ = new QCheckBox("Picture in picture", this);
    pip_->setChecked(true);
    connect(pip_, &QCheckBox::toggled, this, [this]() { applyLayout(); });
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
    applyLayout();
}

void MultiCameraDialog::applyLayout() {
    const int w = std::max(1, stage_->width());
    const int h = std::max(1, stage_->height());
    if (pip_->isChecked()) {
        view_[0]->setGeometry(0, 0, w, h);
        const int iw = std::max(160, w / 3);
        const int ih = std::max(90, h / 3);
        view_[1]->setGeometry(w - iw - 8, h - ih - 8, iw, ih);
        view_[1]->raise();
    } else {
        view_[0]->setGeometry(0, 0, w / 2, h);
        view_[1]->setGeometry(w / 2, 0, w - w / 2, h);
    }
}

void MultiCameraDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    applyLayout();
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
    view_[index]->setPanorama(image);
}

void MultiCameraDialog::save() {
    QImage main = view_[0]->currentView().toImage();
    QImage inset = view_[1]->currentView().toImage();
    if (main.isNull()) return;
    QImage out;
    QPainter painter;
    if (pip_->isChecked()) {
        out = main.copy();
        painter.begin(&out);
        if (!inset.isNull()) {
            const int iw = std::min(out.width() / 3, 420);
            const int ih = iw * inset.height() / std::max(1, inset.width());
            const QRect rect(out.width() - iw - 8, out.height() - ih - 8,
                             iw, ih);
            painter.drawImage(rect, inset.scaled(rect.size(),
                                                 Qt::KeepAspectRatioByExpanding));
            painter.drawRect(rect.adjusted(0, 0, -1, -1));
        }
    } else if (!inset.isNull()) {
        out = QImage(main.width() + inset.width(),
                     std::max(main.height(), inset.height()),
                     QImage::Format_RGB32);
        out.fill(Qt::black);
        painter.begin(&out);
        painter.drawImage(0, 0, main);
        painter.drawImage(main.width(), 0, inset);
    } else {
        out = main.copy();
        painter.begin(&out);
    }
    painter.end();
    const QString path = QFileDialog::getSaveFileName(
        this, "Save multi-camera image", QString(), "PNG (*.png);;JPEG (*.jpg)");
    if (path.isEmpty()) return;
    if (!out.save(path))
        QMessageBox::warning(this, "Save failed", "Could not write " + path);
}

void MultiCameraDialog::saveState() {
    QSettings settings("Photosphere", "multiCamera");
    for (int i = 0; i < 2; ++i) {
        settings.setValue(QString("cam%1/yaw").arg(i), view_[i]->yaw());
        settings.setValue(QString("cam%1/pitch").arg(i), view_[i]->pitch());
        settings.setValue(QString("cam%1/fov").arg(i), view_[i]->fov());
    }
    settings.setValue("pip", pip_->isChecked());
}

void MultiCameraDialog::restoreState() {
    QSettings settings("Photosphere", "multiCamera");
    for (int i = 0; i < 2; ++i) {
        const double yaw = settings.value(QString("cam%1/yaw").arg(i),
                                          i == 1 ? 180.0 : 0.0).toDouble();
        const double pitch = settings.value(QString("cam%1/pitch").arg(i),
                                            0.0).toDouble();
        const double fov = settings.value(QString("cam%1/fov").arg(i),
                                          75.0).toDouble();
        view_[i]->setView(yaw, pitch, fov);
    }
    pip_->setChecked(settings.value("pip", true).toBool());
    applyLayout();
}

void MultiCameraDialog::closeEvent(QCloseEvent* event) {
    saveState();
    QDialog::closeEvent(event);
}

} // namespace ps::gui
