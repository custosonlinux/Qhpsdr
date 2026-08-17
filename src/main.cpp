#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    MainWindow w;
    w.setWindowTitle("Qhpsdr - Next Gen SDR");
    w.resize(1280, 720);
    w.show();

    return app.exec();
}
