#pragma once

#include <QDialog>
#include <QImage>
#include <QPointer>

#include <opencv2/core.hpp>

class QCheckBox;
class QLabel;
class QSlider;
class QTimer;

namespace ps::gui {

class MultiCameraDialog : public QDialog {
    Q_OBJECT

public:
    explicit MultiCameraDialog(const cv::Mat& panorama, QWidget* parent = nullptr);

private slots:
    void preview();
    void save();

private:
    cv::Mat panorama_;
    QImage previewImage_;
    QLabel* previewLabel_ = nullptr;
    QSlider* yawA_ = nullptr;
    QSlider* pitchA_ = nullptr;
    QSlider* yawB_ = nullptr;
    QSlider* pitchB_ = nullptr;
    QSlider* fovA_ = nullptr;
    QSlider* fovB_ = nullptr;
    QCheckBox* pip_ = nullptr;
    QPointer<QTimer> previewTimer_;
};

} // namespace ps::gui
