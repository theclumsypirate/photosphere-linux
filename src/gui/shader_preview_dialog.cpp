#include "gui/shader_preview_dialog.hpp"

#include "gui/shader_playback.hpp"
#include "gui/shader_viewer.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace ps::gui {

ShaderPreviewDialog::ShaderPreviewDialog(const QString& videoPath, double lensFov,
                                         QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("GPU fisheye preview");
    resize(1100, 700);

    viewer_ = new ShaderViewer(this);
    playButton_ = new QPushButton("Play", this);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 1000);
    timeLabel_ = new QLabel("0.0 / 0.0 s", this);
    auto* resetButton = new QPushButton("Reset view", this);

    auto* controls = new QHBoxLayout();
    controls->addWidget(playButton_);
    controls->addWidget(slider_, 1);
    controls->addWidget(timeLabel_);
    controls->addWidget(resetButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(viewer_, 1);
    layout->addLayout(controls);

    playback_ = new ShaderPlayback(this);
    connect(playButton_, &QPushButton::clicked, this,
            &ShaderPreviewDialog::togglePlay);
    connect(slider_, &QSlider::sliderPressed, this, [this]() { scrubbing_ = true; });
    connect(slider_, &QSlider::sliderReleased, this, [this]() {
        scrubbing_ = false;
        playback_->seek(slider_->value() / 1000.0 * playback_->duration());
    });
    connect(playback_, &ShaderPlayback::frameReady, this,
            &ShaderPreviewDialog::onFrame);
    connect(playback_, &ShaderPlayback::positionChanged, this,
            &ShaderPreviewDialog::onPosition);
    connect(playback_, &ShaderPlayback::failed, this,
            &ShaderPreviewDialog::onFailed);
    connect(resetButton, &QPushButton::clicked, viewer_, &ShaderViewer::resetView);

    QString error;
    if (!playback_->open(videoPath, error)) {
        QMessageBox::warning(this, "Preview unavailable", error);
    } else {
        onPosition(0.0);
    }
    (void)lensFov;
}

ShaderPreviewDialog::~ShaderPreviewDialog() { playback_->close(); }

void ShaderPreviewDialog::togglePlay() {
    if (playback_->playing()) {
        playback_->pause();
        playButton_->setText("Play");
    } else {
        playback_->play();
        playButton_->setText("Pause");
    }
}

void ShaderPreviewDialog::onSlider(int value) {
    if (!scrubbing_ || playback_->duration() <= 0.0) return;
    playback_->seek(value / 1000.0 * playback_->duration());
}

void ShaderPreviewDialog::onFrame(const cv::Mat& frame) { viewer_->setFrame(frame); }

void ShaderPreviewDialog::onPosition(double seconds) {
    const double duration = playback_->duration();
    timeLabel_->setText(QString("%1 / %2 s")
                            .arg(seconds, 0, 'f', 1)
                            .arg(duration, 0, 'f', 1));
    if (!scrubbing_ && duration > 0.0)
        slider_->setValue(static_cast<int>(1000.0 * seconds / duration));
}

void ShaderPreviewDialog::onFailed(const QString& message) {
    playButton_->setText("Play");
    QMessageBox::warning(this, "Preview error", message);
}

} // namespace ps::gui
