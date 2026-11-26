#include "gui/main_window.hpp"

#include "core/gpano.hpp"
#include "core/gpu_info.hpp"
#include "core/little_planet.hpp"
#include "core/stitcher.hpp"
#include "io/frame_store.hpp"
#include "io/insv_reader.hpp"

#include <opencv2/imgcodecs.hpp>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
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

QString lastDir() {
    return QString::fromStdString((std::filesystem::path(QDir::homePath().toStdString()) /
                                   "Downloads").string());
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("PhotoSphere Linux");
    resize(1400, 900);

    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    auto* root = new QSplitter(Qt::Horizontal, central);

    // Left: inputs, stitch options and actions.
    auto* controls = new QWidget(root);
    auto* left = new QVBoxLayout(controls);
    left->setContentsMargins(0, 0, 0, 0);
    left->addWidget(new QLabel("Input files (DNG / JPG / INSV)", controls));
    fileList_ = new QListWidget(controls);
    fileList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    left->addWidget(fileList_, 1);

    auto* addButton = new QPushButton("Add images or INSV video", controls);
    auto* removeButton = new QPushButton("Remove selected", controls);
    auto* stitchButton = new QPushButton("Stitch", controls);
    stitchButton->setDefault(true);
    stitchButton->setStyleSheet("font-weight: bold; padding: 6px;");
    left->addWidget(addButton);
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

    multibandCheck_ = new QCheckBox("Multiband blending", controls);
    multibandCheck_->setChecked(true);
    left->addWidget(multibandCheck_);

    flipCombo_ = new QComboBox(controls);
    flipCombo_->addItem("Automatic orientation");
    flipCombo_->addItem("Rotate lenses 180 degrees");
    flipCombo_->addItem("Keep original orientation");
    left->addWidget(new QLabel("Lens orientation"));
    left->addWidget(flipCombo_);

    auto* autoButton = new QPushButton("Auto stitch all files", controls);
    autoButton->setStyleSheet("font-weight: bold;");
    auto* fullscreenButton = new QPushButton("Fullscreen preview", controls);
    auto* savePano = new QPushButton("Save panorama", controls);
    auto* savePlanet = new QPushButton("Save little planet", controls);
    auto* saveTagged = new QPushButton("Save with GPano metadata", controls);
    left->addWidget(autoButton);
    left->addWidget(fullscreenButton);
    left->addWidget(savePano);
    left->addWidget(savePlanet);
    left->addWidget(saveTagged);

    // Right: large panorama on top, log at the bottom.
    auto* right = new QSplitter(Qt::Vertical, root);
    viewer_ = new ViewerWidget(right);
    viewer_->setMinimumHeight(420);
    right->addWidget(viewer_);

    auto* logPanel = new QWidget(right);
    auto* logLayout = new QVBoxLayout(logPanel);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->addWidget(new QLabel("Log (select and copy, for example after a crash)"));
    logEdit_ = new QPlainTextEdit(logPanel);
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumBlockCount(5000);
    logEdit_->setStyleSheet("font-family: monospace; font-size: 11px;");
    logLayout->addWidget(logEdit_, 1);
    auto* logButtons = new QHBoxLayout();
    auto* copyLogButton = new QPushButton("Copy log", logPanel);
    auto* clearLogButton = new QPushButton("Clear log", logPanel);
    logButtons->addWidget(copyLogButton);
    logButtons->addWidget(clearLogButton);
    logButtons->addStretch(1);
    logLayout->addLayout(logButtons);
    right->addWidget(logPanel);
    right->setStretchFactor(0, 4);
    right->setStretchFactor(1, 1);
    right->setSizes({680, 190});

    root->addWidget(controls);
    root->addWidget(right);
    root->setStretchFactor(0, 0);
    root->setStretchFactor(1, 1);
    root->setSizes({330, 1070});
    mainLayout->addWidget(root, 1);

    statusLabel_ = new QLabel("Ready", central);
    mainLayout->addWidget(statusLabel_);
    setCentralWidget(central);

    g_logEdit = logEdit_;
    qInstallMessageHandler(messageHandler);
    logMessage("PhotoSphere Linux started");
    logMessage(QString::fromStdString(describeGpu(detectGpu())));

    connect(addButton, &QPushButton::clicked, this, &MainWindow::addFiles);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeSelected);
    connect(stitchButton, &QPushButton::clicked, this, &MainWindow::runStitch);
    connect(autoButton, &QPushButton::clicked, this, &MainWindow::autoStitchAll);
    connect(fullscreenButton, &QPushButton::clicked, this, &MainWindow::openFullscreen);
    connect(savePano, &QPushButton::clicked, this, &MainWindow::savePanorama);
    connect(savePlanet, &QPushButton::clicked, this, &MainWindow::saveLittlePlanet);
    connect(saveTagged, &QPushButton::clicked, this, &MainWindow::saveTagged);
    connect(copyLogButton, &QPushButton::clicked, [this]() {
        QApplication::clipboard()->setText(logEdit_->toPlainText());
        setStatus("Log copied to clipboard");
    });
    connect(clearLogButton, &QPushButton::clicked, logEdit_, &QPlainTextEdit::clear);
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

void MainWindow::addFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Select images or INSV video", lastDir(),
        "Images and video (*.dng *.jpg *.jpeg *.png *.insv *.mp4 *.mov *.mkv);;"
        "All files (*)");
    for (const auto& file : files) {
        if (fileList_->findItems(file, Qt::MatchExactly).isEmpty()) {
            fileList_->addItem(file);
            logMessage("Added " + file);
        }
    }
}

void MainWindow::removeSelected() {
    for (QListWidgetItem* item : fileList_->selectedItems()) delete item;
}

void MainWindow::runStitch() {
    const auto paths = inputPaths();
    if (paths.empty()) {
        QMessageBox::warning(this, "No input", "Add at least one file.");
        return;
    }
    setStatus("Loading inputs...");
    io::InputSession session;
    std::string error;
    const int maxFrames = maxFramesSpin_->value();
    if (!io::loadInputs(paths, session, error, maxFrames)) {
        setStatus(QString::fromStdString("Error: " + error));
        QMessageBox::critical(this, "Input error", QString::fromStdString(error));
        return;
    }
    logMessage("Loaded " + QString::number(session.frames.size()) + " lens frames: " +
               QString::fromStdString(session.description));

    StitchOptions options;
    options.width = widthSpin_->value();
    options.fisheyeFovDegrees = fovSpin_->value();
    options.multiband = multibandCheck_->isChecked();
    options.flipLenses = flipCombo_->currentIndex() == 1
                             ? true
                             : (flipCombo_->currentIndex() == 2
                                    ? false
                                    : autoFlipForAll(paths));
    setStatus("Stitching " + QString::number(session.frames.size()) + " lens frames...");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::string diagnostics;
    Panorama pano;
    try {
        pano = stitchSession(session, options, diagnostics);
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "unknown exception during stitching";
    }
    QApplication::restoreOverrideCursor();
    if (!error.empty() || pano.equirect.empty()) {
        const QString message = QString::fromStdString(error.empty() ? diagnostics : error);
        setStatus("Error: " + message);
        QMessageBox::critical(this, "Stitch failed", message);
        return;
    }
    panorama_ = pano.equirect;
    viewer_->setPanorama(panorama_);
    viewer_->resetView();
    setStatus(QString::fromStdString(diagnostics));
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
        cv::imwrite(path, item.panorama.equirect);
        logMessage(QString::fromStdString(item.source) + " -> " + QString::fromStdString(path));
        ++index;
    }
    panorama_ = items.front().panorama.equirect;
    viewer_->setPanorama(panorama_);
    viewer_->resetView();
    setStatus("Auto stitch: " + QString::number(index) + " panoramas saved to " + dir);
}

void MainWindow::openFullscreen() {
    if (panorama_.empty()) {
        QMessageBox::warning(this, "Nothing to show", "Stitch first.");
        return;
    }
    auto* window = new FullscreenView(panorama_, this);
    window->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::savePanorama() {
    if (panorama_.empty()) { QMessageBox::warning(this, "Nothing to save", "Stitch first."); return; }
    const QString path = QFileDialog::getSaveFileName(this, "Save panorama", lastDir(), "JPEG (*.jpg)");
    if (path.isEmpty()) return;
    if (cv::imwrite(path.toStdString(), panorama_)) setStatus("Saved " + path);
    else QMessageBox::critical(this, "Save failed", "Could not write the file.");
}

void MainWindow::saveLittlePlanet() {
    if (panorama_.empty()) { QMessageBox::warning(this, "Nothing to save", "Stitch first."); return; }
    const QString path = QFileDialog::getSaveFileName(this, "Save little planet", lastDir(), "PNG (*.png)");
    if (path.isEmpty()) return;
    LittlePlanetOptions options;
    options.outputSize = std::min(4096, panorama_.cols / 2);
    const cv::Mat planet = convertToLittlePlanet(panorama_, options);
    if (cv::imwrite(path.toStdString(), planet)) setStatus("Saved " + path);
    else QMessageBox::critical(this, "Save failed", "Could not write the file.");
}

void MainWindow::saveTagged() {
    if (panorama_.empty()) { QMessageBox::warning(this, "Nothing to save", "Stitch first."); return; }
    const QString path = QFileDialog::getSaveFileName(this, "Save tagged panorama", lastDir(), "JPEG (*.jpg)");
    if (path.isEmpty()) return;
    const QString temp = path + ".tmp.jpg";
    if (!cv::imwrite(temp.toStdString(), panorama_)) {
        QMessageBox::critical(this, "Save failed", "Could not write the temporary file.");
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
