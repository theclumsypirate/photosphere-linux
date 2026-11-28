#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace ps::gui {

class RangeSlider;

struct ExportDialogOptions {
    int width = 2048;
    double fps = 60.0;
    double everySeconds = 1.0;
    double startTime = 0.0;
    double endTime = 0.0;
    QString output;
    bool hevc = false;
    bool spherical = true;
    bool horizonLock = false;
    bool flowState = false;
    bool rotateAudio = false;
    double smoothSeconds = 0.8;
};

class ExportDialog : public QDialog {
    Q_OBJECT

public:
    ExportDialog(double durationSeconds, double sourceFps,
                 const QString& defaultOutput, bool needsStitch = false,
                 QWidget* parent = nullptr);

    ExportDialogOptions options() const;

private slots:
    void chooseOutput();

private:
    QSpinBox* widthSpin_ = nullptr;
    QSpinBox* stepSpin_ = nullptr;
    QDoubleSpinBox* fpsSpin_ = nullptr;
    RangeSlider* range_ = nullptr;
    QLabel* rangeLabel_ = nullptr;
    QLineEdit* outputEdit_ = nullptr;
    QCheckBox* hevcCheck_ = nullptr;
    QCheckBox* sphericalCheck_ = nullptr;
    QCheckBox* horizonCheck_ = nullptr;
    QCheckBox* flowStateCheck_ = nullptr;
    QCheckBox* rotateAudioCheck_ = nullptr;
    QDoubleSpinBox* smoothSpin_ = nullptr;
    double sourceFps_ = 25.0;
};

} // namespace ps::gui
