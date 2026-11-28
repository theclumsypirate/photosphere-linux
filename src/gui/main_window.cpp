#include "gui/main_window.hpp"

#include "core/gpano.hpp"
#include "core/gpu_info.hpp"
#include "gui/export_dialog.hpp"
#include "gui/general_settings.hpp"
#include "gui/hdr_export_worker.hpp"
#include "gui/little_planet_dialog.hpp"
#include "gui/multi_camera_dialog.hpp"
#include "gui/preview_range_dialog.hpp"
#include "gui/save_format_dialog.hpp"
#include "gui/shader_preview_dialog.hpp"
#include "gui/stitch_worker.hpp"
#include "gui/video_export_worker.hpp"
#include "core/little_planet.hpp"
#include "core/stabilize.hpp"
#include "core/stitcher.hpp"
#include "io/exif_pose.hpp"
#include "io/ffmpeg_log.hpp"
#include "io/frame_store.hpp"
#include "io/image_io.hpp"
#include "io/insv_reader.hpp"
#include "io/insv_telemetry.hpp"
#include "io/video_writer.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QMimeData>
#include <QHBoxLayout>
#include <QIcon>
#include <QFont>
#include <QKeySequence>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSplitter>
#include <QStringList>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <sstream>

namespace ps::gui {
namespace {

QPointer<QPlainTextEdit> g_logEdit;

void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    if (!g_logEdit) return;
    if (message.contains("No node found for item that was just removed")) return;
    if (message.contains("setDesktopFileName: the specified desktop file name ends"))
        return;
    if (message.contains("Failed to register with host portal")) return;
    const char* prefix = "LOG";
    if (type == QtWarningMsg) prefix = "WARN";
    else if (type == QtCriticalMsg || type == QtFatalMsg) prefix = "ERROR";
    g_logEdit->appendPlainText(QString("[%1] %2").arg(prefix, message));
}

QString timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm);
    return QString(buffer);
}

// Separate top level window for a larger preview. Escape closes it.
class FullscreenView : public QWidget {
public:
    explicit FullscreenView(const cv::Mat& panorama, QWidget* parent = nullptr)
        : QWidget(parent, Qt::Window | Qt::FramelessWindowHint) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto* viewer = new ViewerWidget(this);
        viewer->setPanorama(panorama);
        layout->addWidget(viewer, 1);
        auto* bar = new QHBoxLayout();
        auto* hint = new QLabel("Drag to look around, wheel to zoom, Esc to close", this);
        hint->setStyleSheet("color: white; padding: 6px;");
        auto* closeButton = new QPushButton("Close", this);
        bar->addWidget(hint, 1);
        bar->addWidget(closeButton);
        layout->addLayout(bar);
        setStyleSheet("background: #0d0d12;");
        connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
        auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(escape, &QShortcut::activated, this, &QWidget::close);
        showFullScreen();
    }
};

bool autoFlipFor(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".insv") == 0)
        return false;
    return true;
}

bool autoFlipForAll(const std::vector<std::string>& paths) {
    for (const auto& p : paths)
        if (!autoFlipFor(p)) return false;
    return true;
}

bool saveImageFile(const QString& path, const cv::Mat& image,
                   const QString& defaultExtension, QString& error) {
    QString target = path;
    const QString name = target.section('/', -1);
    if (!name.contains('.')) target += defaultExtension;
    try {
        if (cv::imwrite(target.toStdString(), image)) return true;
        error = "Could not write " + target;
    } catch (const cv::Exception& e) {
        error = QString::fromLocal8Bit(e.what());
    }
    return false;
}

QString lastDir() {
    return QString::fromStdString((std::filesystem::path(QDir::homePath().toStdString()) /
                                   "Downloads").string());
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Photosphere Linux");
    setWindowIcon(QIcon(":/icons/icon.png"));
    setAcceptDrops(true);
    resize(1780, 1020);
    setMinimumSize(1500, 900);
    appSettings_ = loadAppSettings();
    io::setRawOptions({appSettings_.exposureEV, appSettings_.autoBright});

    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    auto* root = new QSplitter(Qt::Horizontal, central);

    // Left: inputs, stitch options and actions.
    auto* scroll = new QScrollArea(root);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(520);
    auto* controls = new QWidget(scroll);
    controls->setMinimumWidth(500);
    QFont controlFont = controls->font();
    controlFont.setPointSize(11);
    controls->setFont(controlFont);
    auto* left = new QVBoxLayout(controls);
    left->setContentsMargins(4, 4, 4, 4);
    left->setSpacing(6);
    auto* settingsButton = new QPushButton("Settings", controls);
    settingsButton->setIcon(QIcon(":/icons/settings.png"));
    settingsButton->setIconSize(QSize(24, 24));
    settingsButton->setToolTip("General settings");
    settingsButton->setMinimumWidth(120);
    left->addWidget(settingsButton, 0, Qt::AlignRight);
    left->addWidget(new QLabel("Input files, folders or drag and drop", controls));
    fileList_ = new QListWidget(controls);
    fileList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileList_->setMinimumHeight(220);
    left->addWidget(fileList_, 1);

    auto* addButton = new QPushButton("Add images or INSV video", controls);
    auto* addFolderButton = new QPushButton("Add folder of images", controls);
    auto* pasteButton = new QPushButton("Paste files from clipboard", controls);
    auto* removeButton = new QPushButton("Remove selected", controls);
    auto* stitchButton = new QPushButton("Stitch", controls);
    stitchButton->setDefault(true);
    stitchButton->setStyleSheet("font-weight: bold; padding: 6px;");
    left->addWidget(addButton);
    left->addWidget(addFolderButton);
    left->addWidget(pasteButton);
    left->addWidget(removeButton);
    left->addWidget(stitchButton);

    widthSpin_ = new QSpinBox(controls);
    widthSpin_->setRange(1024, 16384);
    widthSpin_->setSingleStep(1024);
    widthSpin_->setValue(4096);
    left->addWidget(new QLabel("Panorama width"));
    left->addWidget(widthSpin_);

    fovSpin_ = new QDoubleSpinBox(controls);
    fovSpin_->setRange(120.0, 230.0);
    fovSpin_->setValue(195.0);
    left->addWidget(new QLabel("Fisheye FOV (degrees)"));
    left->addWidget(fovSpin_);

    maxFramesSpin_ = new QSpinBox(controls);
    maxFramesSpin_->setRange(0, 10000);
    maxFramesSpin_->setValue(1);
    left->addWidget(new QLabel("Video frames (0 = all)"));
    left->addWidget(maxFramesSpin_);

    videoStepSpin_ = new QSpinBox(controls);
    videoStepSpin_->setRange(1, 500);
    videoStepSpin_->setValue(1);
    left->addWidget(new QLabel("Video frame step (1 = every frame)"));
    left->addWidget(videoStepSpin_);

    outputFpsSpin_ = new QSpinBox(controls);
    outputFpsSpin_->setRange(1, 120);
    outputFpsSpin_->setValue(60);
    left->addWidget(new QLabel("Export video fps"));
    left->addWidget(outputFpsSpin_);

    multibandCheck_ = new QCheckBox("Multiband blending", controls);
    multibandCheck_->setChecked(true);
    left->addWidget(multibandCheck_);
    refineCheck_ = new QCheckBox("Refine poses from image features", controls);
    left->addWidget(refineCheck_);
    seamsCheck_ = new QCheckBox("Graph-cut seam carving (experimental)", controls);
    left->addWidget(seamsCheck_);
    warpCheck_ = new QCheckBox("Local warp correction", controls);
    warpCheck_->setChecked(true);
    left->addWidget(warpCheck_);

    flipCombo_ = new QComboBox(controls);
    flipCombo_->addItem("Automatic orientation");
    flipCombo_->addItem("Rotate lenses 180 degrees");
    flipCombo_->addItem("Keep original orientation");
    left->addWidget(new QLabel("Lens orientation"));
    left->addWidget(flipCombo_);

    auto* renderButton = new QPushButton("Render stitched video (MP4)", controls);
    auto* autoButton = new QPushButton("Auto stitch all files", controls);
    autoButton->setStyleSheet("font-weight: bold;");
    auto* fullscreenButton = new QPushButton("Fullscreen preview", controls);
    auto* gpuPreviewButton = new QPushButton("Video preview", controls);
    auto* savePano = new QPushButton("Save panorama", controls);
    auto* multiCamButton = new QPushButton("Multi-camera view", controls);
    auto* savePlanet = new QPushButton("Little planet editor", controls);
    auto* saveTagged = new QPushButton("Save with GPano metadata", controls);
    left->addWidget(renderButton);
    left->addWidget(autoButton);
    left->addWidget(fullscreenButton);
    left->addWidget(gpuPreviewButton);
    left->addWidget(savePano);
    left->addWidget(multiCamButton);
    left->addWidget(savePlanet);
    left->addWidget(saveTagged);

    // Right: large panorama on top, log at the bottom.
    auto* right = new QSplitter(Qt::Vertical, root);
    viewer_ = new ViewerWidget(right);
    viewer_->setMinimumHeight(500);
    right->addWidget(viewer_);

    auto* logPanel = new QWidget(right);
    auto* logLayout = new QVBoxLayout(logPanel);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->addWidget(new QLabel("Log (select and copy, for example after a crash)"));
    logEdit_ = new QPlainTextEdit(logPanel);
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumBlockCount(5000);
    logEdit_->setStyleSheet("font-family: monospace; font-size: 12px;");
    logLayout->addWidget(logEdit_, 1);
    auto* logButtons = new QHBoxLayout();
    auto* copyLogButton = new QPushButton("Copy log", logPanel);
    auto* clearLogButton = new QPushButton("Clear log", logPanel);
    exportButton_ = new QPushButton("Export", logPanel);
    exportButton_->setVisible(false);
    logButtons->addWidget(copyLogButton);
    logButtons->addWidget(clearLogButton);
    logButtons->addStretch(1);
    logButtons->addWidget(exportButton_);
    logLayout->addLayout(logButtons);
    right->addWidget(logPanel);
    right->setStretchFactor(0, 4);
    right->setStretchFactor(1, 1);
    right->setSizes({760, 220});

    scroll->setWidget(controls);
    root->addWidget(scroll);
    root->addWidget(right);
    root->setStretchFactor(0, 0);
    root->setStretchFactor(1, 1);
    root->setSizes({540, 1240});
    mainLayout->addWidget(root, 1);

    statusLabel_ = new QLabel("Ready", central);
    mainLayout->addWidget(statusLabel_);
    setCentralWidget(central);

    g_logEdit = logEdit_;
    qInstallMessageHandler(messageHandler);
    io::setFFmpegLogSink([](const std::string& line) {
        QMetaObject::invokeMethod(qApp, [line]() {
            if (g_logEdit)
                g_logEdit->appendPlainText("[FFMPEG] " + QString::fromStdString(line));
        }, Qt::QueuedConnection);
    });
    logMessage("Photosphere Linux started");
    logMessage(QString::fromStdString(describeGpu(detectGpu())));

    connect(addButton, &QPushButton::clicked, this, &MainWindow::addFiles);
    connect(addFolderButton, &QPushButton::clicked, this, &MainWindow::addFolder);
    connect(pasteButton, &QPushButton::clicked, this, &MainWindow::pasteFromClipboard);
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openGeneralSettings);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeSelected);
    connect(stitchButton, &QPushButton::clicked, this, &MainWindow::runStitch);
    connect(renderButton, &QPushButton::clicked, this, &MainWindow::renderVideo);
    connect(exportButton_, &QPushButton::clicked, this, &MainWindow::renderVideo);
    connect(viewer_, &ViewerWidget::snapshotRequested, this, &MainWindow::saveSnapshot);
    connect(viewer_, &ViewerWidget::panoramaSaveRequested, this, &MainWindow::savePanorama);
    connect(viewer_, &ViewerWidget::stabilizeRequested, this,
            &MainWindow::toggleStabilize);
    connect(multiCamButton, &QPushButton::clicked, this, &MainWindow::openMultiCamera);
    connect(autoButton, &QPushButton::clicked, this, &MainWindow::autoStitchAll);
    connect(fullscreenButton, &QPushButton::clicked, this, &MainWindow::openFullscreen);
    connect(gpuPreviewButton, &QPushButton::clicked, this, &MainWindow::openShaderPreview);
    connect(savePano, &QPushButton::clicked, this, &MainWindow::savePanorama);
    connect(savePlanet, &QPushButton::clicked, this, &MainWindow::saveLittlePlanet);
    connect(saveTagged, &QPushButton::clicked, this, &MainWindow::saveTagged);
    connect(copyLogButton, &QPushButton::clicked, [this]() {
        QApplication::clipboard()->setText(logEdit_->toPlainText());
        setStatus("Log copied to clipboard");
    });
    connect(clearLogButton, &QPushButton::clicked, logEdit_, &QPlainTextEdit::clear);
    auto* fullscreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(fullscreenShortcut, &QShortcut::activated, this,
            &MainWindow::toggleFullscreen);
}

void MainWindow::logMessage(const QString& text) {
    if (logEdit_) logEdit_->appendPlainText("[" + timestamp() + "] " + text);
}

std::vector<std::string> MainWindow::inputPaths() const {
    std::vector<std::string> paths;
    for (int i = 0; i < fileList_->count(); ++i)
        paths.push_back(fileList_->item(i)->text().toStdString());
    return paths;
}

void MainWindow::setStatus(const QString& text) {
    statusLabel_->setText(text);
    logMessage(text);
}

QStringList MainWindow::chooseFilesExternal(bool folder) {
    const bool useZenity = appSettings_.externalCommand.contains("zenity");
    const QString command = useZenity ? "zenity" : "kdialog";
    QStringList args;
    if (command.contains("zenity")) {
        args << (folder ? "--selection" : "--file-selection");
        args << "--filename=" + lastDir() + "/";
        if (!folder) args << "--multiple";
    } else {
        // kdialog, or any unknown value: use the KDE dialog, not the app.
        args << (folder ? "--getexistingdirectory" : "--getopenfilename")
             << lastDir();
        if (!folder)
            args << "--multiple"
                 << "*.dng *.jpg *.jpeg *.png *.insv *.mp4 *.mov *.mkv";
    }
    QProcess process;
    // The AppImage exports LD_LIBRARY_PATH and Qt plugin paths for its own
    // libraries. A system tool such as kdialog must not inherit them.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.remove("LD_LIBRARY_PATH");
    environment.remove("QT_PLUGIN_PATH");
    environment.remove("QT_QPA_PLATFORM_PLUGIN_PATH");
    environment.remove("QT_QPA_PLATFORM");
    process.setProcessEnvironment(environment);
    process.start(command, args);
    if (!process.waitForFinished(-1)) {
        logMessage("Dialog failed to start: " + process.errorString());
        return {};
    }
    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if (output.isEmpty()) {
        const QString errorOutput =
            QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (!errorOutput.isEmpty())
            logMessage("Dialog error: " + errorOutput.left(300));
        return {};
    }
    QStringList paths;
    for (const auto& line : output.split('\n', Qt::SkipEmptyParts)) {
        QString path = line.trimmed();
        if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"'))
            path = path.mid(1, path.size() - 2);
        if (!path.isEmpty()) paths.append(path);
    }
    return paths;
}

void MainWindow::addFiles() {
    if (appSettings_.fileExplorer == "external") {
        const QStringList files = chooseFilesExternal(false);
        if (files.isEmpty()) {
            setStatus("No file selected in the KDE dialog");
            return;
        }
        addPaths(files);
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Select images or INSV video", lastDir(),
        "Images and video (*.dng *.jpg *.jpeg *.png *.insv *.mp4 *.mov *.mkv);;"
        "All files (*)");
    addPaths(files);
}

void MainWindow::addPaths(const QStringList& paths) {
    const QStringList filters{"*.dng", "*.jpg", "*.jpeg", "*.png",
                              "*.insv", "*.mp4", "*.mov", "*.mkv"};
    int added = 0;
    for (const auto& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            const QStringList names = QDir(path).entryList(filters, QDir::Files);
            for (const auto& name : names) {
                const QString child = QDir(path).filePath(name);
                if (fileList_->findItems(child, Qt::MatchExactly).isEmpty()) {
                    fileList_->addItem(child);
                    ++added;
                }
            }
        } else if (info.isFile()) {
            if (fileList_->findItems(path, Qt::MatchExactly).isEmpty()) {
                fileList_->addItem(path);
                ++added;
            }
        }
    }
    if (added > 0)
        logMessage("Added " + QString::number(added) + " file(s)");
    updateExportButton();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData() && event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList paths;
    if (event->mimeData())
        for (const auto& url : event->mimeData()->urls())
            if (url.isLocalFile()) paths.append(url.toLocalFile());
    addPaths(paths);
    event->acceptProposedAction();
}

void MainWindow::addFolder() {
    if (appSettings_.fileExplorer == "external") {
        const QStringList folders = chooseFilesExternal(true);
        if (folders.isEmpty()) {
            setStatus("No folder selected in the KDE dialog");
            return;
        }
        QStringList allFiles;
        for (const auto& folder : folders) {
            const QStringList names = QDir(folder).entryList(
                {"*.dng", "*.jpg", "*.jpeg", "*.png", "*.insv",
                 "*.mp4", "*.mov", "*.mkv"}, QDir::Files);
            for (const auto& name : names)
                allFiles.append(QDir(folder).filePath(name));
        }
        addPaths(allFiles);
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Select a folder of photos or videos", lastDir());
    if (dir.isEmpty()) return;
    const QStringList names = QDir(dir).entryList(
        {"*.jpg", "*.jpeg", "*.png", "*.dng", "*.insv", "*.mp4", "*.mov"},
        QDir::Files);
    std::vector<std::pair<int, QString>> ordered;
    ordered.reserve(static_cast<size_t>(names.size()));
    int fallback = 100000;
    for (const auto& name : names) {
        const QString path = QDir(dir).filePath(name);
        io::ExifPose pose;
        const int key = io::readExifPose(path.toStdString(), pose) && pose.index >= 0
                            ? pose.index
                            : fallback++;
        ordered.emplace_back(key, path);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    int added = 0;
    for (const auto& [key, path] : ordered) {
        if (fileList_->findItems(path, Qt::MatchExactly).isEmpty()) {
            fileList_->addItem(path);
            ++added;
        }
    }
    logMessage("Added " + QString::number(added) + " files from " + dir +
               " in capture order");
    updateExportButton();
}

void MainWindow::openGeneralSettings() {
    GeneralSettingsDialog dialog(this);
    bool needsRestart = false;
    connect(&dialog, &GeneralSettingsDialog::restartRequested, this,
            [&needsRestart]() { needsRestart = true; });
    if (dialog.exec() == QDialog::Accepted) {
        appSettings_ = dialog.settings();
        saveAppSettings(appSettings_);
        io::setRawOptions({appSettings_.exposureEV, appSettings_.autoBright});
        io::Mp4Writer::setPreferHardwareEncoder(appSettings_.preferGpu);
        setStatus("Settings saved. File explorer: " +
                  (appSettings_.fileExplorer == "external"
                       ? appSettings_.externalCommand
                       : "Qt file dialog"));
        if (needsRestart) {
            QMessageBox box(this);
            box.setWindowTitle("Restart required");
            box.setText("The GPU or display backend setting changes on the next start.");
            box.setInformativeText("Restart Photosphere now?");
            auto* restart = box.addButton("Restart now", QMessageBox::AcceptRole);
            box.addButton("Later", QMessageBox::RejectRole);
            box.exec();
            if (box.clickedButton() == restart) {
                const QString program = QCoreApplication::applicationFilePath();
                QProcess::startDetached(program, QStringList());
                QCoreApplication::quit();
            }
        }
    }
}

void MainWindow::pasteFromClipboard() {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls()) {
        setStatus("Clipboard has no file paths");
        return;
    }
    QStringList paths;
    for (const auto& url : mime->urls())
        if (url.isLocalFile()) paths.append(url.toLocalFile());
    addPaths(paths);
}

void MainWindow::removeSelected() {
    for (QListWidgetItem* item : fileList_->selectedItems()) delete item;
    updateExportButton();
}

void MainWindow::updateExportButton() {
    bool hasVideo = false;
    for (const auto& path : inputPaths())
        if (io::isVideoFile(path)) hasVideo = true;
    exportButton_->setVisible(hasVideo);
}

void MainWindow::runStitch() {
    if (stitchThread_) {
        QMessageBox::information(this, "Stitching", "A stitch is already running.");
        return;
    }
    const auto paths = inputPaths();
    if (paths.empty()) {
        QMessageBox::warning(this, "No input", "Add at least one file.");
        return;
    }

    StitchWorker::Options options;
    options.paths = paths;
    options.maxVideoFrames = std::max(1, maxFramesSpin_->value());
    options.stitch.width = widthSpin_->value();
    options.stitch.fisheyeFovDegrees = fovSpin_->value();
    options.stitch.multiband = multibandCheck_->isChecked();
    options.stitch.refinePoses = refineCheck_->isChecked();
    options.stitch.useSeams = seamsCheck_->isChecked();
    options.stitch.useLocalWarp = warpCheck_->isChecked();
    options.stitch.flipLenses = flipCombo_->currentIndex() == 1
                                    ? true
                                    : (flipCombo_->currentIndex() == 2
                                           ? false
                                           : autoFlipForAll(paths));

    stitchThread_ = new QThread(this);
    auto* worker = new StitchWorker();
    worker->moveToThread(stitchThread_);
    worker->setOptions(options);

    stitchProgress_ = new QProgressDialog("Preparing stitch...", QString(), 0, 10, this);
    stitchProgress_->setWindowModality(Qt::WindowModal);
    stitchProgress_->setMinimumDuration(0);
    stitchProgress_->setValue(0);

    connect(stitchThread_, &QThread::started, worker, &StitchWorker::run);
    connect(worker, &StitchWorker::progress, this, [this](int done, int total,
                                                          const QString& message) {
        if (!stitchProgress_) return;
        stitchProgress_->setMaximum(std::max(1, total));
        stitchProgress_->setValue(done);
        stitchProgress_->setLabelText(message);
    });
    connect(worker, &StitchWorker::finished, this,
            [this, worker](bool ok, const QString& message) {
                if (ok && !worker->result().equirect.empty()) {
                    panorama_ = worker->result().equirect;
                    viewer_->setPanorama(panorama_);
                    viewer_->resetView();
                } else if (!ok) {
                    QMessageBox::critical(this, "Stitch failed", message);
                }
                setStatus(message);
                stitchThread_->quit();
            });
    connect(stitchThread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(stitchThread_, &QThread::finished, this, [this]() {
        stitchThread_->deleteLater();
        stitchThread_ = nullptr;
        if (stitchProgress_) {
            stitchProgress_->close();
            stitchProgress_->deleteLater();
            stitchProgress_ = nullptr;
        }
    });
    stitchThread_->start();
}

void MainWindow::autoStitchAll() {
    const auto paths = inputPaths();
    if (paths.empty()) {
        QMessageBox::warning(this, "No input", "Add at least one file.");
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Output directory for panoramas", lastDir());
    if (dir.isEmpty()) return;

    StitchOptions options;
    options.width = widthSpin_->value();
    options.fisheyeFovDegrees = fovSpin_->value();
    options.multiband = multibandCheck_->isChecked();
    options.refinePoses = refineCheck_->isChecked();
    options.useSeams = seamsCheck_->isChecked();
    options.useLocalWarp = warpCheck_->isChecked();
    options.flipLenses = flipCombo_->currentIndex() == 1
                             ? true
                             : (flipCombo_->currentIndex() == 2
                                    ? false
                                    : autoFlipForAll(paths));

    setStatus("Auto processing " + QString::number(paths.size()) + " file(s)...");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::vector<io::BatchItem> items;
    std::string error;
    try {
        io::stitchAllInputs(paths, options, items, error, maxFramesSpin_->value());
    } catch (const std::exception& e) {
        error += std::string("exception: ") + e.what() + "\n";
    }
    QApplication::restoreOverrideCursor();
    if (!error.empty()) logMessage(QString::fromStdString("Warnings:\n" + error));
    if (items.empty()) {
        setStatus("Auto stitch produced no panoramas");
        QMessageBox::critical(this, "Auto stitch failed", QString::fromStdString(error));
        return;
    }
    int index = 0;
    for (const auto& item : items) {
        const std::string name = "pano_" +
                                 std::string(3 - std::to_string(index).size(), '0') +
                                 std::to_string(index) + ".jpg";
        const std::string path = (std::filesystem::path(dir.toStdString()) / name).string();
        QString saveError;
        const bool saved = saveImageFile(QString::fromStdString(path),
                                         item.panorama.equirect, ".jpg", saveError);
        if (!saved) logMessage("Save failed: " + saveError);
        logMessage(QString::fromStdString(item.source) + " -> " + QString::fromStdString(path));
        ++index;
    }
    panorama_ = items.front().panorama.equirect;
    viewer_->setPanorama(panorama_);
    viewer_->resetView();
    setStatus("Auto stitch: " + QString::number(index) + " panoramas saved to " + dir);
}

void MainWindow::renderVideo() {
    if (exportThread_) {
        QMessageBox::information(this, "Export running", "An export is already running.");
        return;
    }
    std::string videoPath;
    for (const auto& path : inputPaths())
        if (io::isVideoFile(path)) { videoPath = path; break; }
    if (videoPath.empty()) {
        QMessageBox::warning(this, "No video", "Add a video file first.");
        return;
    }
    const double duration = io::insvDuration(videoPath);
    const double sourceFps = io::insvFrameRate(videoPath);
    const QString defaultOutput = lastDir() + "/photosphere-export.mp4";
    ExportDialog dialog(duration, sourceFps, defaultOutput, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const ExportDialogOptions dialogOptions = dialog.options();
    if (dialogOptions.output.isEmpty()) return;

    VideoExportWorker::Options options;
    options.input = videoPath;
    options.output = dialogOptions.output.toStdString();
    options.width = dialogOptions.width;
    options.fps = dialogOptions.fps;
    options.everySeconds = dialogOptions.everySeconds;
    options.startTime = dialogOptions.startTime;
    options.endTime = dialogOptions.endTime;
    options.flipLenses = autoFlipFor(videoPath);
    if (flipCombo_->currentIndex() == 1) options.flipLenses = true;
    if (flipCombo_->currentIndex() == 2) options.flipLenses = false;
    options.multiband = multibandCheck_->isChecked();
    options.hevc = dialogOptions.hevc;
    options.spherical = dialogOptions.spherical;
    options.horizonLock = dialogOptions.horizonLock;
    options.flowState = dialogOptions.flowState;
    options.rotateAudio = dialogOptions.rotateAudio;
    options.smoothSeconds = dialogOptions.smoothSeconds;

    exportCancel_ = false;
    exportThread_ = new QThread(this);
    auto* worker = new VideoExportWorker();
    worker->moveToThread(exportThread_);
    worker->setOptions(options);
    worker->setCancelFlag(&exportCancel_);

    exportProgress_ = new QProgressDialog("Preparing export...", "Cancel", 0, 100, this);
    exportProgress_->setWindowModality(Qt::WindowModal);
    exportProgress_->setMinimumDuration(0);
    exportProgress_->setValue(0);

    connect(exportThread_, &QThread::started, worker, &VideoExportWorker::run);
    connect(worker, &VideoExportWorker::progress, this, [this](int done, int total,
                                                                const QString& message) {
        if (!exportProgress_) return;
        exportProgress_->setMaximum(std::max(1, total));
        exportProgress_->setValue(done);
        exportProgress_->setLabelText(message);
    });
    connect(exportProgress_, &QProgressDialog::canceled, this, [this]() {
        exportCancel_ = true;
        setStatus("Export cancellation requested");
    });
    connect(worker, &VideoExportWorker::finished, this,
            [this, worker](bool ok, const QString& message) {
                setStatus(message);
                logMessage(message);
                if (!ok && message != "Export cancelled")
                    QMessageBox::warning(this, "Export", message);
                exportThread_->quit();
            });
    connect(exportThread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(exportThread_, &QThread::finished, this, [this]() {
        exportThread_->deleteLater();
        exportThread_ = nullptr;
        if (exportProgress_) {
            exportProgress_->close();
            exportProgress_->deleteLater();
            exportProgress_ = nullptr;
        }
    });
    exportThread_->start();
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) showNormal();
    else showFullScreen();
}

void MainWindow::openFullscreen() {
    if (panorama_.empty()) {
        QMessageBox::warning(this, "Nothing to show", "Stitch first.");
        return;
    }
    auto* window = new FullscreenView(panorama_, this);
    window->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::quickExport() {
    if (panorama_.empty()) {
        renderVideo();
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Export panorama with GPano metadata", lastDir(), "JPEG (*.jpg)");
    if (path.isEmpty()) return;
    const QString temp = path + ".tmp.jpg";
    QString saveError;
    if (!saveImageFile(temp, panorama_, ".jpg", saveError)) {
        QMessageBox::critical(this, "Export failed", saveError);
        return;
    }
    std::string error;
    const bool ok = injectGpano(temp.toStdString(), path.toStdString(),
                                GPanoMetadata::forFullPano(panorama_.cols, panorama_.rows),
                                error);
    std::filesystem::remove(temp.toStdString());
    if (ok) setStatus("Exported " + path);
    else QMessageBox::critical(this, "Export failed", QString::fromStdString(error));
}

void MainWindow::saveSnapshot() {
    if (panorama_.empty()) {
        QMessageBox::warning(this, "Nothing to capture", "Stitch first.");
        return;
    }
    const QPixmap view = viewer_->currentView();
    if (view.isNull()) {
        QMessageBox::warning(this, "Snapshot failed", "Could not read the OpenGL view.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Save current view", lastDir(), "PNG (*.png);;JPEG (*.jpg)");
    if (path.isEmpty()) return;
    if (view.save(path)) setStatus("Saved view " + path);
    else QMessageBox::critical(this, "Snapshot failed", "Could not write the file.");
}

void MainWindow::savePanorama() {
    if (panorama_.empty()) { QMessageBox::warning(this, "Nothing to save", "Stitch first."); return; }
    SaveFormatDialog formatDialog(this);
    if (formatDialog.exec() != QDialog::Accepted) return;
    const bool exr = formatDialog.format() == SaveFormat::Exr;
    const QString path = QFileDialog::getSaveFileName(
        this, exr ? "Save HDR panorama" : "Save panorama", lastDir(),
        exr ? "OpenEXR (*.exr)" : "JPEG (*.jpg)");
    if (path.isEmpty()) return;
    if (!exr) {
        QString saveError;
        if (saveImageFile(path, panorama_, ".jpg", saveError)) setStatus("Saved " + path);
        else QMessageBox::critical(this, "Save failed", saveError);
        return;
    }
    std::vector<std::string> rawPaths;
    for (const auto& pathString : inputPaths())
        if (pathString.size() > 4 && pathString.substr(pathString.size() - 4) == ".dng")
            rawPaths.push_back(pathString);
    if (rawPaths.empty()) {
        QMessageBox::warning(this, "No RAW input",
                             "Add the bracketed DNG files before saving EXR.");
        return;
    }
    if (hdrThread_) { QMessageBox::information(this, "Export", "HDR export is running."); return; }

    HdrExportWorker::Options options;
    options.paths = rawPaths;
    options.output = path.toStdString();
    options.stitch.width = std::min(4096, panorama_.cols);
    options.stitch.fisheyeFovDegrees = fovSpin_->value();
    options.stitch.flipLenses = autoFlipForAll(rawPaths);
    if (flipCombo_->currentIndex() == 1) options.stitch.flipLenses = true;
    if (flipCombo_->currentIndex() == 2) options.stitch.flipLenses = false;

    hdrThread_ = new QThread(this);
    auto* worker = new HdrExportWorker();
    worker->moveToThread(hdrThread_);
    worker->setOptions(options);
    connect(hdrThread_, &QThread::started, worker, &HdrExportWorker::run);
    connect(worker, &HdrExportWorker::finished, this,
            [this](bool ok, const QString& message) {
                setStatus(message);
                if (!ok) QMessageBox::warning(this, "HDR export failed", message);
                hdrThread_->quit();
            });
    connect(hdrThread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(hdrThread_, &QThread::finished, this, [this]() {
        hdrThread_->deleteLater();
        hdrThread_ = nullptr;
    });
    setStatus("Merging HDR bracket...");
    hdrThread_->start();
}

void MainWindow::openShaderPreview() {
    std::string videoPath;
    for (const auto& path : inputPaths())
        if (io::isVideoFile(path)) { videoPath = path; break; }
    if (videoPath.empty()) {
        videoPath = QFileDialog::getOpenFileName(
            this, "Choose a 360 video", lastDir(),
            "360 video (*.insv *.mp4 *.mov *.mkv);;All files (*)")
                        .toStdString();
    }
    if (videoPath.empty()) return;
    if (io::isEacVideo(videoPath)) {
        QMessageBox::information(this, "GPU preview",
                                 "GoPro EAC files are already stitched. Use the "
                                 "panorama viewer after loading a frame.");
        return;
    }
    double from = 0.0;
    double to = io::insvDuration(videoPath);
    if (to > 0.0) {
        PreviewRangeDialog rangeDialog(to, this);
        if (rangeDialog.exec() != QDialog::Accepted) return;
        from = rangeDialog.startSeconds();
        to = rangeDialog.endSeconds();
    }
    auto* dialog = new ShaderPreviewDialog(QString::fromStdString(videoPath),
                                           fovSpin_->value(), from, to, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->show();
}

void MainWindow::toggleStabilize() {
    if (panorama_.empty()) {
        QMessageBox::information(this, "Stabilize", "Stitch a panorama first.");
        return;
    }
    if (stabilized_) {
        panorama_ = originalPanorama_;
        stabilized_ = false;
        viewer_->setPanorama(panorama_);
        setStatus("Stabilization disabled");
        return;
    }
    std::string videoPath;
    for (const auto& path : inputPaths())
        if (io::isVideoFile(path)) { videoPath = path; break; }
    if (videoPath.empty()) {
        QMessageBox::information(this, "Stabilize",
                                 "Add the source .insv video first.");
        return;
    }
    io::InsvTelemetry telemetry;
    std::string error;
    if (!io::readInsvTelemetry(videoPath, telemetry, error)) {
        QMessageBox::warning(this, "Stabilize",
                             QString::fromStdString(error));
        return;
    }
    StabilizeOptions options;
    options.mode = StabilizeMode::HorizonLock;
    Stabilizer stabilizer;
    if (!stabilizer.load(telemetry, options, error)) {
        QMessageBox::warning(this, "Stabilize",
                             QString::fromStdString(error));
        return;
    }
    originalPanorama_ = panorama_.clone();
    panorama_ = rotateEquirect(panorama_, stabilizer.rotationAt(0.0));
    stabilized_ = true;
    viewer_->setPanorama(panorama_);
    setStatus("Horizon leveled using IMU");
}

void MainWindow::openMultiCamera() {
    if (panorama_.empty()) {
        QMessageBox::warning(this, "Nothing to show", "Stitch a photo first.");
        return;
    }
    auto* dialog = new MultiCameraDialog(panorama_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->show();
}

void MainWindow::saveLittlePlanet() {
    if (panorama_.empty()) { QMessageBox::warning(this, "Nothing to save", "Stitch first."); return; }
    auto* dialog = new LittlePlanetDialog(panorama_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->show();
}

void MainWindow::saveTagged() {
    if (panorama_.empty()) { QMessageBox::warning(this, "Nothing to save", "Stitch first."); return; }
    const QString path = QFileDialog::getSaveFileName(this, "Save tagged panorama", lastDir(), "JPEG (*.jpg)");
    if (path.isEmpty()) return;
    const QString temp = path + ".tmp.jpg";
    QString saveError;
    if (!saveImageFile(temp, panorama_, ".jpg", saveError)) {
        QMessageBox::critical(this, "Save failed", saveError);
        return;
    }
    std::string error;
    const bool ok = injectGpano(temp.toStdString(), path.toStdString(),
                                GPanoMetadata::forFullPano(panorama_.cols, panorama_.rows), error);
    std::filesystem::remove(temp.toStdString());
    if (ok) setStatus("Saved " + path);
    else QMessageBox::critical(this, "Save failed", QString::fromStdString(error));
}

} // namespace ps::gui
