#include "gui/general_settings.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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
    out.exposureEV = settings.value("exposureEV", 0.0).toDouble();
    out.autoBright = settings.value("autoBright", true).toBool();
    return out;
}

void saveAppSettings(const AppSettings& settings) {
    QSettings store(kOrganization, kApplication);
    store.setValue("fileExplorer", settings.fileExplorer);
    store.setValue("externalCommand", settings.externalCommand);
    store.setValue("exposureEV", settings.exposureEV);
    store.setValue("autoBright", settings.autoBright);
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

    exposureSpin_ = new QDoubleSpinBox(this);
    exposureSpin_->setRange(-5.0, 5.0);
    exposureSpin_->setSingleStep(0.25);
    exposureSpin_->setDecimals(2);
    exposureSpin_->setSuffix(" EV");
    exposureSpin_->setValue(current.exposureEV);

    autoBrightCheck_ = new QCheckBox("Auto brightness for RAW (DNG)", this);
    autoBrightCheck_->setChecked(current.autoBright);

    auto* form = new QFormLayout();
    form->addRow(new QLabel("Optional: change your default file explorer for opening"),
                 explorerCombo_);
    form->addRow(new QLabel("External explorer command"), commandEdit_);
    form->addRow(new QLabel("RAW exposure adjustment"), exposureSpin_);
    form->addRow(autoBrightCheck_);

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
    out.exposureEV = exposureSpin_->value();
    out.autoBright = autoBrightCheck_->isChecked();
    return out;
}

} // namespace ps::gui
