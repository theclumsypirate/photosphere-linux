#include "gui/tourist_filter_dialog.hpp"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace ps::gui {

TouristFilterDialog::TouristFilterDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Remove tourists from photos");
    resize(700, 480);

    folderEdit_ = new QLineEdit(this);
    folderEdit_->setPlaceholderText("Folder with aligned .jpg photos");
    auto* folderButton = new QPushButton("Browse...", this);

    methodCombo_ = new QComboBox(this);
    methodCombo_->addItems({"CUT", "CUT_AND_MEDIAN", "MEDIAN",
                            "NOISE_AND_MEDIAN", "INPAINT_AND_MEDIAN"});
    methodCombo_->setCurrentText("MEDIAN");

    toolEdit_ = new QLineEdit(
        QDir::homePath() + "/.local/share/photosphere/tourist-filter", this);
    auto* toolButton = new QPushButton("Browse...", this);

    resultEdit_ = new QLineEdit(this);
    resultEdit_->setReadOnly(true);
    resultEdit_->setPlaceholderText("Output will appear here");
    auto* openButton = new QPushButton("Open result", this);

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    log_->setStyleSheet("font-family: monospace; font-size: 11px;");

    runButton_ = new QPushButton("Run", this);
    cancelButton_ = new QPushButton("Cancel", this);
    cancelButton_->setEnabled(false);

    auto* folderRow = new QHBoxLayout();
    folderRow->addWidget(new QLabel("Photos", this));
    folderRow->addWidget(folderEdit_, 1);
    folderRow->addWidget(folderButton);
    auto* methodRow = new QHBoxLayout();
    methodRow->addWidget(new QLabel("Method", this));
    methodRow->addWidget(methodCombo_, 1);
    auto* toolRow = new QHBoxLayout();
    toolRow->addWidget(new QLabel("Tool", this));
    toolRow->addWidget(toolEdit_, 1);
    toolRow->addWidget(toolButton);
    auto* resultRow = new QHBoxLayout();
    resultRow->addWidget(new QLabel("Result", this));
    resultRow->addWidget(resultEdit_, 1);
    resultRow->addWidget(openButton);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(runButton_);
    buttons->addWidget(cancelButton_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(folderRow);
    layout->addLayout(methodRow);
    layout->addLayout(toolRow);
    layout->addLayout(resultRow);
    layout->addWidget(new QLabel("Log", this));
    layout->addWidget(log_, 1);
    layout->addLayout(buttons);

    connect(folderButton, &QPushButton::clicked, this,
            &TouristFilterDialog::chooseFolder);
    connect(toolButton, &QPushButton::clicked, this,
            &TouristFilterDialog::chooseTool);
    connect(openButton, &QPushButton::clicked, this, [this]() {
        const QString path = resultEdit_->text();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(runButton_, &QPushButton::clicked, this, &TouristFilterDialog::run);
    connect(cancelButton_, &QPushButton::clicked, this,
            &TouristFilterDialog::cancel);
}

TouristFilterDialog::~TouristFilterDialog() {
    if (process_) {
        process_->kill();
        process_->waitForFinished(1000);
        delete process_;
    }
}

QString TouristFilterDialog::toolFolder() const {
    return toolEdit_->text().trimmed();
}

void TouristFilterDialog::setRunning(bool running) {
    runButton_->setEnabled(!running);
    cancelButton_->setEnabled(running);
    folderEdit_->setEnabled(!running);
    toolEdit_->setEnabled(!running);
    methodCombo_->setEnabled(!running);
}

void TouristFilterDialog::chooseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Photo stack folder", folderEdit_->text());
    if (!dir.isEmpty()) folderEdit_->setText(dir);
}

void TouristFilterDialog::chooseTool() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Tourist filter folder", toolFolder());
    if (!dir.isEmpty()) toolEdit_->setText(dir);
}

void TouristFilterDialog::run() {
    const QString folder = folderEdit_->text().trimmed();
    if (!QDir(folder).exists()) {
        log_->appendPlainText("Choose a valid folder of aligned photos.");
        return;
    }
    if (!QFile::exists(toolFolder() + "/filter-all-tourists.py")) {
        log_->appendPlainText("Tourist filter not found in: " + toolFolder());
        return;
    }
    const QString python = QStandardPaths::findExecutable("python3");
    if (python.isEmpty()) {
        log_->appendPlainText("python3 was not found in PATH.");
        return;
    }

    resultEdit_->clear();
    log_->clear();
    log_->appendPlainText("Processing " + folder + " with " +
                          methodCombo_->currentText() + "...");
    if (process_) delete process_;
    process_ = new QProcess(this);
    process_->setWorkingDirectory(toolFolder());
    connect(process_, &QProcess::readyReadStandardOutput, this,
            &TouristFilterDialog::onOutput);
    connect(process_, &QProcess::readyReadStandardError, this,
            &TouristFilterDialog::onOutput);
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TouristFilterDialog::onFinished);
    connect(process_, &QProcess::errorOccurred, this,
            &TouristFilterDialog::onError);
    process_->start(python,
                    {"-u", toolFolder() + "/filter-all-tourists.py", "-i",
                     folder, "-m", methodCombo_->currentText(), "-n"});
    setRunning(true);
}

void TouristFilterDialog::cancel() {
    if (process_ && process_->state() != QProcess::NotRunning)
        process_->kill();
    log_->appendPlainText("Cancelled.");
}

void TouristFilterDialog::onOutput() {
    if (!process_) return;
    const QString out = QString::fromUtf8(process_->readAllStandardOutput());
    const QString err = QString::fromUtf8(process_->readAllStandardError());
    if (!out.isEmpty()) log_->appendPlainText(out.trimmed());
    if (!err.isEmpty()) log_->appendPlainText(err.trimmed());
}

void TouristFilterDialog::onError() {
    if (process_) log_->appendPlainText("Error: " + process_->errorString());
}

void TouristFilterDialog::onFinished(int exitCode) {
    setRunning(false);
    log_->appendPlainText("Finished with code " + QString::number(exitCode));
    if (exitCode == 0) showResult();
    if (process_) {
        process_->deleteLater();
        process_ = nullptr;
    }
}

void TouristFilterDialog::showResult() {
    const QString folder = folderEdit_->text().trimmed();
    const QFileInfo info(folder);
    QDir dir(info.absolutePath());
    dir.setFilter(QDir::Files);
    dir.setNameFilters({info.fileName() + "_yolo_*.jpg",
                        info.fileName() + "_*.jpg"});
    dir.setSorting(QDir::Time | QDir::Reversed);
    const QStringList files = dir.entryList();
    if (files.isEmpty()) {
        log_->appendPlainText("No output image was found.");
        return;
    }
    const QString result = dir.filePath(files.first());
    resultEdit_->setText(result);
    log_->appendPlainText("Result: " + result);
}

} // namespace ps::gui
