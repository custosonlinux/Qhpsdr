#include "mainwindow.h"
#include <QMenuBar>
#include <QStatusBar>
#include <QDebug>

extern "C" {
    #include "radio.h"
    #include "discovery.h"
}

#include <QMenuBar>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    // Initial UI Setup
    auto *menu = menuBar()->addMenu(tr("&Radio"));
    auto *exitAction = menu->addAction(tr("E&xit"));
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

        statusBar()->showMessage(tr("Qhpsdr Ready."));

    // Start Hardware Discovery (Async)
    qDebug() << "Starting SDR Discovery...";
    // discovery_init(); // Wir müssen erst sicherstellen, dass alle Symbole linken
}
}

MainWindow::~MainWindow() {}
