#include "gui/main_window.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QProcess>
#include <QStandardPaths>

#include <cstdio>

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

// The AppImage carries a desktop file and icon. On first run, install them
// into the user's data directories so the desktop search finds the app.
void installDesktopIntegration() {
    const QString appdir = QString::fromLocal8Bit(qgetenv("APPDIR"));
    const QString exeDir = QCoreApplication::applicationDirPath();
    QString sourceDesktop;
    QString sourceIcon;

    if (!appdir.isEmpty()) {
        sourceDesktop = appdir + "/usr/share/applications/photosphere-linux.desktop";
        sourceIcon = appdir + "/usr/share/icons/hicolor/512x512/apps/photosphere-linux.png";
    } else {
        const QStringList candidates{
            exeDir + "/../share/applications/photosphere-linux.desktop",
            exeDir + "/assets/photosphere-linux.desktop",
            QDir::currentPath() + "/assets/photosphere-linux.desktop",
        };
        for (const auto& candidate : candidates)
            if (QFile::exists(candidate)) { sourceDesktop = candidate; break; }
        sourceIcon = exeDir + "/../share/icons/hicolor/512x512/apps/photosphere-linux.png";
        if (!QFile::exists(sourceIcon))
            sourceIcon = exeDir + "/assets/icon.png";
    }
    if (!QFile::exists(sourceDesktop)) return;

    const QString appsDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    const QString iconsDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                             "/icons/hicolor/512x512/apps";
    if (appsDir.isEmpty()) return;
    QDir().mkpath(appsDir);
    QDir().mkpath(iconsDir);
    const QString targetDesktop = appsDir + "/photosphere-linux.desktop";
    const QString targetIcon = iconsDir + "/photosphere-linux.png";
    if (!QFile::exists(targetDesktop)) QFile::copy(sourceDesktop, targetDesktop);
    if (QFile::exists(sourceIcon) && !QFile::exists(targetIcon))
        QFile::copy(sourceIcon, targetIcon);
    QProcess::startDetached("update-desktop-database", {appsDir});
}

} // namespace

int main(int argc, char** argv) {
    qInstallMessageHandler(earlyMessageHandler);
    QApplication app(argc, argv);
    // The desktop file name becomes the Wayland app id.
    QGuiApplication::setDesktopFileName("photosphere-linux.desktop");
    app.setApplicationName("PhotoSphere Linux");
    app.setWindowIcon(QIcon(":/icons/icon.png"));
    installDesktopIntegration();
    ps::gui::MainWindow window;
    window.show();
    return app.exec();
}
