#include "mainwindow.h"
#include <QMenuBar>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    // Initial UI Setup
    auto *menu = menuBar()->addMenu(tr("&Radio"));
    auto *exitAction = menu->addAction(tr("E&xit"));
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

    statusBar()->showMessage(tr("Qhpsdr Ready."));
}

MainWindow::~MainWindow() {}
