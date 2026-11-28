#pragma once

#include <QDialog>

#include <opencv2/core.hpp>

class QCheckBox;
class QPushButton;
class QResizeEvent;
class QWidget;

namespace ps::gui {

class ViewerWidget;

// Two virtual cameras sharing the same GPU renderer as the main panorama view.
class MultiCameraDialog : public QDialog {
    Q_OBJECT

public:
    explicit MultiCameraDialog(const cv::Mat& panorama,
                               QWidget* parent = nullptr);
    MultiCameraDialog(const cv::Mat& first, const cv::Mat& second,
                      QWidget* parent = nullptr);
    ~MultiCameraDialog() override;

private slots:
    void loadPicture(int index);
    void save();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void build();
    void saveState();
    void restoreState();
    void applyLayout();

    cv::Mat panorama_[2];
    ViewerWidget* view_[2] = {nullptr, nullptr};
    QWidget* stage_ = nullptr;
    QCheckBox* pip_ = nullptr;
};

} // namespace ps::gui
