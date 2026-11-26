#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLineEdit;

namespace ps::gui {

struct AppSettings {
    QString fileExplorer = "qt"; // "qt" or "external"
    QString externalCommand = "dolphin";
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
};

} // namespace ps::gui
