#include "mainwindow.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QApplication>

#include "discoverydialog.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    auto *radioMenu = menuBar()->addMenu(tr("&Radio"));

    auto *discoverAction = radioMenu->addAction(tr("&Discover..."));
    connect(discoverAction, &QAction::triggered, this, &MainWindow::showDiscoveryDialog);

    radioMenu->addSeparator();

    auto *exitAction = radioMenu->addAction(tr("E&xit"));
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

    statusBar()->showMessage(tr("Qhpsdr Ready."));
}

MainWindow::~MainWindow() = default;

void MainWindow::showDiscoveryDialog() {
    DiscoveryDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted && dialog.selectedDevice()) {
        const auto &device = *dialog.selectedDevice();
        statusBar()->showMessage(tr("Selected %1 at %2").arg(device.name, device.address.toString()));
    }
}
