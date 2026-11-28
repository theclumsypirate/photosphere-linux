#pragma once

#include <QDialog>
#include <QString>

#include <opencv2/core.hpp>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

namespace ps::gui {

class ShaderPlayback;
class ShaderViewer;

// GPU preview window with playback and a timeline slider.
class ShaderPreviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShaderPreviewDialog(const QString& videoPath, double lensFov,
                                 double fromSeconds, double toSeconds,
                                 QWidget* parent = nullptr);
    ~ShaderPreviewDialog() override;

private slots:
    void togglePlay();
    void onSlider(int value);
    void onFrame(const cv::Mat& frame);
    void onPosition(double seconds);
    void onFailed(const QString& message);

private:
    ShaderViewer* viewer_ = nullptr;
    ShaderPlayback* playback_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QComboBox* speedCombo_ = nullptr;
    QSlider* slider_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    bool scrubbing_ = false;
};

} // namespace ps::gui
