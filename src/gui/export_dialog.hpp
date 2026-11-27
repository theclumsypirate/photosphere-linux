#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

namespace ps::gui {

struct ExportDialogOptions {
    int width = 2048;
    double fps = 60.0;
    double everySeconds = 1.0;
    double startTime = 0.0;
    double endTime = 0.0;
    QString output;
    bool hevc = false;
    bool spherical = true;
};

class ExportDialog : public QDialog {
    Q_OBJECT

public:
    ExportDialog(double durationSeconds, double sourceFps,
                 const QString& defaultOutput, QWidget* parent = nullptr);

    ExportDialogOptions options() const;

private slots:
    void chooseOutput();

private:
    QSpinBox* widthSpin_ = nullptr;
    QSpinBox* stepSpin_ = nullptr;
    QDoubleSpinBox* fpsSpin_ = nullptr;
    QDoubleSpinBox* startSpin_ = nullptr;
    QDoubleSpinBox* endSpin_ = nullptr;
    QLineEdit* outputEdit_ = nullptr;
    QCheckBox* hevcCheck_ = nullptr;
    QCheckBox* sphericalCheck_ = nullptr;
    double sourceFps_ = 25.0;
};

} // namespace ps::gui
