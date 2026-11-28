#include "gui/general_settings.hpp"
#include "gui/main_window.hpp"
#include "gui/shader_preview_dialog.hpp"

#include "io/video_writer.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QSettings>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Qt emits this warning before the main window installs its own handler.
void earlyMessageHandler(QtMsgType type, const QMessageLogContext&,
                         const QString& message) {
    if (message.contains("Failed to register with host portal")) return;
    if (message.contains("setDesktopFileName: the specified desktop file name ends"))
        return;
    std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
    (void)type;
}

} // namespace

int main(int argc, char** argv) {
    setenv("OPENCV_IO_ENABLE_OPENEXR", "1", 1);
    qInstallMessageHandler(earlyMessageHandler);
    const ps::gui::AppSettings settings = ps::gui::loadAppSettings();
    if (settings.forceX11) qputenv("QT_QPA_PLATFORM", "xcb");
    if (settings.softwareRendering) qputenv("QT_QUICK_BACKEND", "software");
    if (!settings.preferGpu) qputenv("PS_DISABLE_GPU", "1");
    ps::io::Mp4Writer::setPreferHardwareEncoder(settings.preferGpu);

    QApplication app(argc, argv);
    // libmpv requires the C numeric locale; Qt switches it to the user's
    // locale at startup, which makes mpv_create() fail.
    std::setlocale(LC_NUMERIC, "C");
    // The desktop file name becomes the Wayland app id.
    QGuiApplication::setDesktopFileName("photosphere-linux.desktop");
    app.setApplicationName("Photosphere Linux");
    app.setWindowIcon(QIcon(":/icons/icon.png"));
    QString previewFile;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--preview") == 0) previewFile = argv[++i];
    if (!previewFile.isEmpty()) {
        ps::gui::ShaderPreviewDialog dialog(previewFile, 195.0);
        dialog.show();
        return app.exec();
    }
    ps::gui::MainWindow window;
    window.show();
    return app.exec();
}
