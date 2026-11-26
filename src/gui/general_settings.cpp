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
    out.externalCommand = settings.value("externalCommand", "kdialog").toString();
    out.exposureEV = settings.value("exposureEV", 0.0).toDouble();
    out.autoBright = settings.value("autoBright", true).toBool();
    out.forceX11 = settings.value("forceX11", false).toBool();
    out.softwareRendering = settings.value("softwareRendering", false).toBool();
    out.preferGpu = settings.value("preferGpu", true).toBool();
    return out;
}

void saveAppSettings(const AppSettings& settings) {
    QSettings store(kOrganization, kApplication);
    store.setValue("fileExplorer", settings.fileExplorer);
    store.setValue("externalCommand", settings.externalCommand);
    store.setValue("exposureEV", settings.exposureEV);
    store.setValue("autoBright", settings.autoBright);
    store.setValue("forceX11", settings.forceX11);
    store.setValue("softwareRendering", settings.softwareRendering);
    store.setValue("preferGpu", settings.preferGpu);
    store.sync();
}

GeneralSettingsDialog::GeneralSettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("General settings");
    setMinimumWidth(460);
    const AppSettings current = loadAppSettings();

    explorerCombo_ = new QComboBox(this);
    explorerCombo_->addItem("Qt file dialog", "qt");
    explorerCombo_->addItem("Dolphin / KDE file dialog (kdialog)", "external");
    const int index = explorerCombo_->findData(current.fileExplorer);
    if (index >= 0) explorerCombo_->setCurrentIndex(index);

    commandEdit_ = new QLineEdit(current.externalCommand, this);
    commandEdit_->setPlaceholderText("kdialog");

    exposureSpin_ = new QDoubleSpinBox(this);
    exposureSpin_->setRange(-5.0, 5.0);
    exposureSpin_->setSingleStep(0.25);
    exposureSpin_->setDecimals(2);
    exposureSpin_->setSuffix(" EV");
    exposureSpin_->setValue(current.exposureEV);

    autoBrightCheck_ = new QCheckBox("Auto brightness for RAW (DNG)", this);
    autoBrightCheck_->setChecked(current.autoBright);

    forceX11Check_ = new QCheckBox("Force X11/XWayland (fixes Qt6 crashes)", this);
    forceX11Check_->setChecked(current.forceX11);
    softwareCheck_ = new QCheckBox("Disable graphics acceleration (software rendering)", this);
    softwareCheck_->setChecked(current.softwareRendering);
    gpuCheck_ = new QCheckBox("Prefer GPU for video export when available", this);
    gpuCheck_->setChecked(current.preferGpu);

    auto* form = new QFormLayout();
    form->addRow(new QLabel("Optional: change your default file explorer for opening"),
                 explorerCombo_);
    form->addRow(new QLabel("External explorer command"), commandEdit_);
    form->addRow(new QLabel("RAW exposure adjustment"), exposureSpin_);
    form->addRow(autoBrightCheck_);
    form->addRow(forceX11Check_);
    form->addRow(softwareCheck_);
    form->addRow(gpuCheck_);

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
    if (out.externalCommand.isEmpty()) out.externalCommand = "kdialog";
    out.exposureEV = exposureSpin_->value();
    out.autoBright = autoBrightCheck_->isChecked();
    out.forceX11 = forceX11Check_->isChecked();
    out.softwareRendering = softwareCheck_->isChecked();
    out.preferGpu = gpuCheck_->isChecked();
    return out;
}

} // namespace ps::gui
