// SPDX-License-Identifier: MIT
// qv-vulkan-settings: the settings app of the quad views layer.
#include <QApplication>
#include <QIcon>
#include <QTimer>

#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("qv-vulkan-settings");
    QApplication::setApplicationDisplayName("Quad Views for Vulkan");
    QApplication::setDesktopFileName("qv-vulkan-settings");
    QApplication::setWindowIcon(QIcon::fromTheme("preferences-desktop-display"));
    qvgui::MainWindow w;
    // --screenshot FILE: render the window to a PNG and quit (for the docs and for checking the layout).
    const QStringList args = QApplication::arguments();
    if (const qsizetype i = args.indexOf("--screenshot"); i >= 0 && i + 1 < args.size()) {
        w.show();
        QTimer::singleShot(300, [&] { app.exit(w.grab().save(args[i + 1]) ? 0 : 1); });
        return app.exec();
    }
    w.show();
    return app.exec();
}
