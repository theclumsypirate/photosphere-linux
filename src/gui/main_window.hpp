#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include <opencv2/core.hpp>

#include <vector>
#include <string>

#include "gui/viewer_widget.hpp"

namespace ps::gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

public slots:
    void addPaths(const QStringList& paths);

private slots:
    void addFiles();
    void addFolder();
    void removeSelected();
    void runStitch();
    void autoStitchAll();
    void renderVideo();
    void quickExport();
    void savePanorama();
    void saveLittlePlanet();
    void saveTagged();
    void openFullscreen();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    std::vector<std::string> inputPaths() const;
    void setStatus(const QString& text);
    void logMessage(const QString& text);

    QListWidget* fileList_ = nullptr;
    QSpinBox* widthSpin_ = nullptr;
    QDoubleSpinBox* fovSpin_ = nullptr;
    QSpinBox* maxFramesSpin_ = nullptr;
    QSpinBox* videoStepSpin_ = nullptr;
    QSpinBox* outputFpsSpin_ = nullptr;
    QCheckBox* multibandCheck_ = nullptr;
    QCheckBox* refineCheck_ = nullptr;
    QCheckBox* seamsCheck_ = nullptr;
    QCheckBox* warpCheck_ = nullptr;
    QComboBox* flipCombo_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPlainTextEdit* logEdit_ = nullptr;
    ViewerWidget* viewer_ = nullptr;
    cv::Mat panorama_;
};

} // namespace ps::gui
