#pragma once

#include <QDialog>
#include <QImage>
#include <QPointer>
#include <QString>

#include <opencv2/core.hpp>

#include "core/multi_view.hpp"

class QCheckBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSlider;
class QTimer;

namespace ps::gui {

// Two virtual cameras from one or two equirectangular panoramas. Drag on the
// preview to aim the selected camera; no yaw or pitch spin boxes.
class MultiCameraDialog : public QDialog {
    Q_OBJECT

public:
    explicit MultiCameraDialog(const cv::Mat& panorama, QWidget* parent = nullptr);
    MultiCameraDialog(const cv::Mat& first, const cv::Mat& second,
                      QWidget* parent = nullptr);
    explicit MultiCameraDialog(const QString& videoPath, QWidget* parent = nullptr);

private slots:
    void preview();
    void save();
    void capture(int index);
    void onTimeChanged(int value);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void build();
    cv::Mat currentPanorama() const;
    struct Camera {
        double yaw = 0.0;
        double pitch = 0.0;
        double fov = 75.0;
        operator VirtualCamera() const { return {yaw, pitch, fov}; }
    };

    cv::Mat panorama_[2];
    Camera camera_[2];
    int selected_ = 0;
    QString videoPath_;
    double videoDuration_ = 0.0;

    QImage previewImage_;
    QLabel* previewLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QRadioButton* editFirst_ = nullptr;
    QRadioButton* editSecond_ = nullptr;
    QSlider* fov_ = nullptr;
    QSlider* time_ = nullptr;
    QCheckBox* pip_ = nullptr;
    QPointer<QTimer> previewTimer_;
    QPoint lastPos_;
};

} // namespace ps::gui
