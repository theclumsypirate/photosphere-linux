#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;

namespace ps::gui {

struct AppSettings {
    QString fileExplorer = "qt"; // "qt" or "external"
    QString externalCommand = "dolphin";
    double exposureEV = 0.0;
    bool autoBright = true;
};

AppSettings loadAppSettings();
void saveAppSettings(const AppSettings& settings);

class GeneralSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit GeneralSettingsDialog(QWidget* parent = nullptr);

    AppSettings settings() const;

private:
    QComboBox* explorerCombo_ = nullptr;
    QLineEdit* commandEdit_ = nullptr;
    QDoubleSpinBox* exposureSpin_ = nullptr;
    QCheckBox* autoBrightCheck_ = nullptr;
};

} // namespace ps::gui
