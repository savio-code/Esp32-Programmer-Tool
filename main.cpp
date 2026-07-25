#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ESP32 Flasher & Encryption Manager");

    MainWindow w;
    w.show();

    return app.exec();
}