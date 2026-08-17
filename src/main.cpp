#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // Fixes the QStandardPaths::AppDataLocation directory RxAudioChannel
    // caches FFTW wisdom in (see core/rxaudio.cpp) - without this it
    // defaults to the executable's file name, which isn't guaranteed
    // stable across build/install layouts.
    QCoreApplication::setOrganizationName("Qhpsdr");
    QCoreApplication::setApplicationName("Qhpsdr");

    MainWindow w;
    w.setWindowTitle("Qhpsdr - Next Gen SDR");
    w.resize(1280, 720);
    w.show();

    return app.exec();
}
