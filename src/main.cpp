// main.cpp - Arch Housekeeper entry point.
#include <QApplication>
#include <QMessageBox>
#include <QStandardPaths>

#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("arch-housekeeper"));
    QApplication::setApplicationName(QStringLiteral("Arch Housekeeper"));
    QApplication::setApplicationVersion(QStringLiteral("1.1"));
    QApplication::setDesktopFileName(QStringLiteral("arch-housekeeper"));

    if (QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
        QMessageBox::critical(nullptr, QApplication::applicationName(),
                              QObject::tr("pacman wasn't found. This app only works on "
                                          "Arch Linux and distributions based on it."));
        return 1;
    }

    MainWindow window;
    window.show();
    return app.exec();
}
