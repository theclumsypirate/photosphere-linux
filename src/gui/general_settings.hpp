#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace ps::gui {

struct AppSettings {
    QString fileExplorer = "qt"; // "qt" or "external"
    QString externalCommand = "kdialog";
    double exposureEV = 0.0;
    bool autoBright = true;
    bool forceX11 = false;
    bool softwareRendering = false;
    bool preferGpu = true;
};

AppSettings loadAppSettings();
void saveAppSettings(const AppSettings& settings);

class GeneralSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit GeneralSettingsDialog(QWidget* parent = nullptr);

    AppSettings settings() const;

private slots:
    void checkForUpdates();
    void openLatestRelease(const QString& link);

private:
    QComboBox* explorerCombo_ = nullptr;
    QLineEdit* commandEdit_ = nullptr;
    QDoubleSpinBox* exposureSpin_ = nullptr;
    QCheckBox* autoBrightCheck_ = nullptr;
    QCheckBox* forceX11Check_ = nullptr;
    QCheckBox* softwareCheck_ = nullptr;
    QCheckBox* gpuCheck_ = nullptr;
    QLabel* updateLabel_ = nullptr;
    QString latestReleaseUrl_;
    QString currentVersion_ = "1.0.1";
};

} // namespace ps::gui
