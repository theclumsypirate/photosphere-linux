#include "gui/general_settings.hpp"
#include "gui/main_window.hpp"

#include "io/video_writer.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QSettings>

#include <cstdio>
#include <cstdlib>

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
    qInstallMessageHandler(earlyMessageHandler);
    const ps::gui::AppSettings settings = ps::gui::loadAppSettings();
    if (settings.forceX11) qputenv("QT_QPA_PLATFORM", "xcb");
    if (settings.softwareRendering) qputenv("QT_QUICK_BACKEND", "software");
    if (!settings.preferGpu) qputenv("PS_DISABLE_GPU", "1");
    ps::io::Mp4Writer::setPreferHardwareEncoder(settings.preferGpu);

    QApplication app(argc, argv);
    // The desktop file name becomes the Wayland app id.
    QGuiApplication::setDesktopFileName("photosphere-linux.desktop");
    app.setApplicationName("PhotoSphere Linux");
    app.setWindowIcon(QIcon(":/icons/icon.png"));
    ps::gui::MainWindow window;
    window.show();
    return app.exec();
}
