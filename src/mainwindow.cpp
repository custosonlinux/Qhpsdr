#include "mainwindow.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QMenuBar>
#include <QStatusBar>
#include <QApplication>

#include "discoverydialog.h"
#include "oldprotocol.h"
#include "rxaudio.h"

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

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);
    m_audioDevice = m_audioSink->start();

    m_rxAudio = new RxAudioChannel(this);
    connect(m_rxAudio, &RxAudioChannel::opened, this, [this]() {
        statusBar()->showMessage(tr("Audio engine ready."));
    });
    m_rxAudio->open(/*channel=*/0, /*inputSampleRate=*/48000);
    statusBar()->showMessage(tr("Opening audio engine (first run can take a while)..."));

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
    connect(m_connection, &OldProtocolConnection::iqSamplesReady, this, [this](const QVector<double> &iq) {
        for (int i = 0; i + 1 < iq.size(); i += 2) {
            m_rxAudio->feedSample(iq[i], iq[i + 1]);
        }
    });
    connect(m_rxAudio, &RxAudioChannel::audioBlockReady, this, &MainWindow::playAudioBlock);

    m_connection->connectToDevice(device);
    m_disconnectAction->setEnabled(true);
}

void MainWindow::playAudioBlock(const QVector<float> &interleavedStereo) {
    if (!m_audioDevice) {
        return;
    }
    m_audioDevice->write(reinterpret_cast<const char *>(interleavedStereo.constData()),
                          interleavedStereo.size() * qint64(sizeof(float)));
}

void MainWindow::disconnectFromRadio() {
    if (m_connection) {
        m_connection->disconnectFromDevice();
        m_connection->deleteLater();
        m_connection = nullptr;
    }
    if (m_rxAudio) {
        m_rxAudio->close();
        m_rxAudio->deleteLater();
        m_rxAudio = nullptr;
    }
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
        m_audioDevice = nullptr;
    }
    m_disconnectAction->setEnabled(false);
    statusBar()->showMessage(tr("Disconnected."));
}
