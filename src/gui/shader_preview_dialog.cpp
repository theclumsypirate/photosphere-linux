#include "gui/shader_preview_dialog.hpp"

#include "gui/shader_playback.hpp"
#include "gui/shader_viewer.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace ps::gui {

ShaderPreviewDialog::ShaderPreviewDialog(const QString& videoPath, double lensFov,
                                         double fromSeconds, double toSeconds,
                                         QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Video preview");
    resize(1100, 700);

    viewer_ = new ShaderViewer(this);
    viewer_->setLensFov(lensFov);
    playback_ = new ShaderPlayback(this);

    playButton_ = new QPushButton("Pause", this);
    speedCombo_ = new QComboBox(this);
    for (const QString& label : {"0.5x", "1x", "2x", "4x", "8x", "16x"})
        speedCombo_->addItem(label);
    speedCombo_->setCurrentText("1x");
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 1000);
    timeLabel_ = new QLabel("0.0 s", this);
    statusLabel_ = new QLabel("Decoding...", this);

    auto* controls = new QHBoxLayout();
    controls->addWidget(playButton_);
    controls->addWidget(speedCombo_);
    controls->addWidget(slider_, 1);
    controls->addWidget(timeLabel_);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(viewer_, 1);
    layout->addLayout(controls);
    layout->addWidget(statusLabel_);

    connect(playButton_, &QPushButton::clicked, this,
            &ShaderPreviewDialog::togglePlay);
    connect(speedCombo_, &QComboBox::currentTextChanged, this,
            [this](const QString& text) {
                playback_->setSpeed(text.chopped(1).toDouble());
            });
    connect(slider_, &QSlider::sliderPressed, this,
            [this]() { scrubbing_ = true; });
    connect(slider_, &QSlider::sliderReleased, this, [this]() {
        scrubbing_ = false;
        playback_->seek(slider_->value() / 1000.0 * playback_->duration());
    });
    connect(slider_, &QSlider::valueChanged, this,
            &ShaderPreviewDialog::onSlider);
    connect(playback_, &ShaderPlayback::frameReady, this,
            &ShaderPreviewDialog::onFrame);
    connect(playback_, &ShaderPlayback::positionChanged, this,
            &ShaderPreviewDialog::onPosition);
    connect(playback_, &ShaderPlayback::failed, this,
            &ShaderPreviewDialog::onFailed);

    QString error;
    if (!playback_->open(videoPath, fromSeconds, toSeconds, error)) {
        statusLabel_->setText(error);
        playButton_->setText("Play");
    }
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
    timeLabel_->setText(QString("%1 / %2 s")
                            .arg(value / 1000.0 * playback_->duration(), 0, 'f', 1)
                            .arg(playback_->duration(), 0, 'f', 1));
}

void ShaderPreviewDialog::onFrame(const cv::Mat& frame) {
    viewer_->setFrame(frame);
    statusLabel_->setText("Playing");
}

void ShaderPreviewDialog::onPosition(double seconds) {
    timeLabel_->setText(QString("%1 / %2 s")
                            .arg(seconds, 0, 'f', 1)
                            .arg(playback_->duration(), 0, 'f', 1));
    if (!scrubbing_ && playback_->duration() > 0.0)
        slider_->setValue(static_cast<int>(1000.0 * seconds /
                                           playback_->duration()));
}

void ShaderPreviewDialog::onFailed(const QString& message) {
    statusLabel_->setText(message);
    playButton_->setText("Play");
}

} // namespace ps::gui
