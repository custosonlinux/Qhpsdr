#include "mainwindow.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QApplication>

#include "discoverydialog.h"
#include "oldprotocol.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    auto *radioMenu = menuBar()->addMenu(tr("&Radio"));

    auto *discoverAction = radioMenu->addAction(tr("&Discover..."));
    connect(discoverAction, &QAction::triggered, this, &MainWindow::showDiscoveryDialog);

    m_disconnectAction = radioMenu->addAction(tr("D&isconnect"));
    m_disconnectAction->setEnabled(false);
    connect(m_disconnectAction, &QAction::triggered, this, &MainWindow::disconnectFromRadio);

    radioMenu->addSeparator();

    auto *exitAction = radioMenu->addAction(tr("E&xit"));
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

    statusBar()->showMessage(tr("Qhpsdr Ready."));
}

MainWindow::~MainWindow() = default;

void MainWindow::showDiscoveryDialog() {
    DiscoveryDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || !dialog.selectedDevice()) {
        return;
    }

    const auto &device = *dialog.selectedDevice();
    if (device.protocol != ORIGINAL_PROTOCOL) {
        statusBar()->showMessage(
            tr("%1 uses Protocol 2, which isn't wired up yet - only Protocol 1 devices can be connected.")
                .arg(device.name));
        return;
    }

    disconnectFromRadio();

    m_connection = new OldProtocolConnection(this);
    connect(m_connection, &OldProtocolConnection::statsUpdated, this,
            [this, device](quint64 packets, quint64 samples, double sps) {
                Q_UNUSED(packets);
                Q_UNUSED(samples);
                statusBar()->showMessage(
                    tr("Connected to %1 at %2 - %3 samples/s").arg(device.name, device.address.toString()).arg(sps, 0, 'f', 0));
            });
    connect(m_connection, &OldProtocolConnection::errorOccurred, this, [this](const QString &msg) {
        statusBar()->showMessage(tr("Radio connection error: %1").arg(msg));
    });

    m_connection->connectToDevice(device);
    m_disconnectAction->setEnabled(true);
}

void MainWindow::disconnectFromRadio() {
    if (!m_connection) {
        return;
    }
    m_connection->disconnectFromDevice();
    m_connection->deleteLater();
    m_connection = nullptr;
    m_disconnectAction->setEnabled(false);
    statusBar()->showMessage(tr("Disconnected."));
}
