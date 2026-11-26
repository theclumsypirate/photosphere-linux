#include "gui/general_settings.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QVBoxLayout>

namespace ps::gui {

namespace {
const char* kOrganization = "theclumsypirate";
const char* kApplication = "photosphere-linux";
}

AppSettings loadAppSettings() {
    QSettings settings(kOrganization, kApplication);
    AppSettings out;
    out.fileExplorer = settings.value("fileExplorer", "qt").toString();
    out.externalCommand = settings.value("externalCommand", "dolphin").toString();
    return out;
}

void saveAppSettings(const AppSettings& settings) {
    QSettings store(kOrganization, kApplication);
    store.setValue("fileExplorer", settings.fileExplorer);
    store.setValue("externalCommand", settings.externalCommand);
    store.sync();
}

GeneralSettingsDialog::GeneralSettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("General settings");
    setMinimumWidth(460);
    const AppSettings current = loadAppSettings();

    explorerCombo_ = new QComboBox(this);
    explorerCombo_->addItem("Qt file dialog", "qt");
    explorerCombo_->addItem("Dolphin / external file explorer", "external");
    const int index = explorerCombo_->findData(current.fileExplorer);
    if (index >= 0) explorerCombo_->setCurrentIndex(index);

    commandEdit_ = new QLineEdit(current.externalCommand, this);
    commandEdit_->setPlaceholderText("dolphin");

    auto* form = new QFormLayout();
    form->addRow(new QLabel("Optional: change your default file explorer for opening"),
                 explorerCombo_);
    form->addRow(new QLabel("External explorer command"), commandEdit_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("General settings"));
    layout->addLayout(form);
    layout->addWidget(buttons);
}

AppSettings GeneralSettingsDialog::settings() const {
    AppSettings out;
    out.fileExplorer = explorerCombo_->currentData().toString();
    out.externalCommand = commandEdit_->text().trimmed();
    if (out.externalCommand.isEmpty()) out.externalCommand = "dolphin";
    return out;
}

} // namespace ps::gui
