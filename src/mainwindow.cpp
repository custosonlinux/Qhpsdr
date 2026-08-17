#include "mainwindow.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    auto *radioMenu = menuBar()->addMenu(tr("&Radio"));
    auto *exitAction = radioMenu->addAction(tr("E&xit"));
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

    statusBar()->showMessage(tr("Qhpsdr Ready."));
}

MainWindow::~MainWindow() = default;
