#include "gui/main_window.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // The desktop file name becomes the Wayland app id, so the compositor
    // shows the correct taskbar icon instead of a generic one.
    QGuiApplication::setDesktopFileName("photosphere-linux.desktop");
    app.setApplicationName("PhotoSphere Linux");
    app.setWindowIcon(QIcon(":/icons/icon.png"));
    ps::gui::MainWindow window;
    window.show();
    return app.exec();
}
