#include "gui/general_settings.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace ps::gui {

namespace {
const char* kOrganization = "theclumsypirate";
const char* kApplication = "photosphere-linux";
const char* kCurrentVersion = "v1.0.1";
const char* kLatestReleaseUrl =
    "https://api.github.com/repos/theclumsypirate/photosphere-linux/releases/latest";
}

AppSettings loadAppSettings() {
    QSettings settings(kOrganization, kApplication);
    AppSettings out;
    out.fileExplorer = settings.value("fileExplorer", "qt").toString();
    out.externalCommand = settings.value("externalCommand", "kdialog").toString();
    // Older versions stored "dolphin" here. That was the app, not a dialog.
    if (out.externalCommand.isEmpty() || out.externalCommand.contains("dolphin"))
        out.externalCommand = "kdialog";
    out.exposureEV = settings.value("exposureEV", 0.0).toDouble();
    out.autoBright = settings.value("autoBright", true).toBool();
    out.forceX11 = settings.value("forceX11", false).toBool();
    out.softwareRendering = settings.value("softwareRendering", false).toBool();
    out.preferGpu = settings.value("preferGpu", true).toBool();
    out.useVulkan = settings.value("useVulkan", false).toBool();
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
    store.setValue("useVulkan", settings.useVulkan);
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
    gpuCheck_ = new QCheckBox("Use GPU when available (video decode and preview)", this);
    gpuCheck_->setChecked(current.preferGpu);
    vulkanCheck_ = new QCheckBox("Use Vulkan GPU (experimental, falls back if unsupported)", this);
    vulkanCheck_->setChecked(current.useVulkan);

    auto* form = new QFormLayout();
    form->addRow(new QLabel("Optional: change your default file explorer for opening"),
                 explorerCombo_);
    form->addRow(new QLabel("External explorer command"), commandEdit_);
    form->addRow(new QLabel("RAW exposure adjustment"), exposureSpin_);
    form->addRow(autoBrightCheck_);
    form->addRow(forceX11Check_);
    form->addRow(softwareCheck_);
    form->addRow(gpuCheck_);
    form->addRow(vulkanCheck_);

    auto* updateLayout = new QHBoxLayout();
    auto* checkButton = new QPushButton("Check for updates", this);
    currentVersion_ = QString(kCurrentVersion).remove('v');
    updateLabel_ = new QLabel("Current version: " + QString(kCurrentVersion), this);
    updateLabel_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    updateLabel_->setOpenExternalLinks(false);
    connect(updateLabel_, &QLabel::linkActivated, this,
            &GeneralSettingsDialog::openLatestRelease);
    updateLayout->addWidget(checkButton);
    updateLayout->addWidget(updateLabel_, 1);
    connect(checkButton, &QPushButton::clicked, this,
            &GeneralSettingsDialog::checkForUpdates);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("General settings"));
    layout->addLayout(form);
    layout->addLayout(updateLayout);
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
    out.useVulkan = vulkanCheck_->isChecked();
    return out;
}

void GeneralSettingsDialog::checkForUpdates() {
    updateLabel_->setText("Checking GitHub...");
    auto* manager = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(kLatestReleaseUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "photosphere-linux");
    QNetworkReply* reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        reply->deleteLater();
        manager->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            updateLabel_->setText("Update check failed: " + reply->errorString());
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = object.value("tag_name").toString();
        if (tag.isEmpty()) {
            updateLabel_->setText("No release information found");
            return;
        }
        const QString url = object.value("html_url").toString();
        latestReleaseUrl_ = url;
        const QRegularExpression versionPattern("\\d+(?:\\.\\d+)+");
        const QRegularExpressionMatch match = versionPattern.match(tag);
        if (!match.hasMatch()) {
            updateLabel_->setText("Release tag: " + tag.toHtmlEscaped());
            return;
        }
        const QVersionNumber latest = QVersionNumber::fromString(match.captured());
        const QVersionNumber current = QVersionNumber::fromString(currentVersion_);
        if (QVersionNumber::compare(latest, current) <= 0) {
            updateLabel_->setText("You are up to date (v" + currentVersion_ + ")");
        } else if (url.isEmpty()) {
            updateLabel_->setText("New version available: " + tag.toHtmlEscaped());
        } else {
            updateLabel_->setText(
                "<a href=\"" + url.toHtmlEscaped() + "\">New version " +
                latest.toString() + " - open release</a>");
        }
    });
}

void GeneralSettingsDialog::openLatestRelease(const QString& link) {
    if (!link.isEmpty()) QDesktopServices::openUrl(QUrl(link));
}

} // namespace ps::gui
