#include <QApplication>
#include <QMainWindow>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    MainWindow w;
    w.setWindowTitle("Qhpsdr - Next Gen SDR");
    w.resize(1280, 720);
    w.show();
    return a.exec();
}
