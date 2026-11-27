#pragma once

#include <QDialog>
#include <QImage>
#include <QPointer>

#include <opencv2/core.hpp>

class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace ps::gui {

class LittlePlanetDialog : public QDialog {
    Q_OBJECT

public:
    explicit LittlePlanetDialog(const cv::Mat& panorama, QWidget* parent = nullptr);

private slots:
    void preview();
    void save();

private:
    cv::Mat panorama_;
    QImage previewImage_;
    QLabel* previewLabel_ = nullptr;
    QSlider* fov_ = nullptr;
    QSlider* tilt_ = nullptr;
    QSlider* pan_ = nullptr;
    QSlider* spin_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPointer<QTimer> previewTimer_;
};

} // namespace ps::gui
