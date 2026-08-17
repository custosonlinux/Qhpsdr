#include "mainwindow.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QMenuBar>
#include <QStatusBar>
#include <QApplication>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>
#include <cmath>

#include "discoverydialog.h"
#include "oldprotocol.h"
#include "panadapterwidget.h"
#include "rxaudio.h"
#include "spectrumanalyzer.h"
#include "vfopanel.h"
#include "waterfallwidget.h"

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

    // Layout mirrors deskHPSDR's radio_create_visual() stacking order
    // (core/deskhpsdr-src/radio.c): VFO bar, then the receiver panel
    // (panadapter above waterfall, sharing the frequency axis). Zoom/pan,
    // sliders and the button toolbar aren't ported yet.
    m_vfoPanel = new VfoPanel(this);

    m_panadapter = new PanadapterWidget(this);
    m_waterfall = new WaterfallWidget(this);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_panadapter);
    splitter->addWidget(m_waterfall);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_vfoPanel);
    layout->addWidget(splitter, /*stretch=*/1);
    setCentralWidget(central);

    connect(m_vfoPanel, &VfoPanel::frequencyEditedHz, this, [this](double hz) {
        if (m_connection) {
            m_connection->setRxFrequency(hz);
        }
        m_panadapter->setCenterFrequencyHz(hz);
    });
    connect(m_vfoPanel, &VfoPanel::modeSelected, this, [this](RxMode mode) {
        if (m_rxAudio) {
            m_rxAudio->setMode(mode);
        }
    });

    m_spectrum = new SpectrumAnalyzer(this);
    connect(m_spectrum, &SpectrumAnalyzer::spectrumReady, this, [this](const QVector<float> &db) {
        m_panadapter->setSpectrum(db);
        m_waterfall->pushSpectrum(db);
    });
    m_panadapter->setCenterFrequencyHz(m_vfoPanel->frequencyHz());
    m_panadapter->setSampleRateHz(48000.0);

    statusBar()->showMessage(tr("Qhpsdr Ready."));
}

MainWindow::~MainWindow() = default;

void MainWindow::showDiscoveryDialog() {
    DiscoveryDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || !dialog.selectedDevice()) {
        return;
    }

    // A real copy, not a reference: selectedDevice() returns
    // std::optional<DiscoveredDevice> by value, so a reference bound
    // through the dereferenced temporary would dangle as soon as this
    // statement ends.
    const DiscoveredDevice device = *dialog.selectedDevice();
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
            m_spectrum->feedSample(iq[i], iq[i + 1]);
        }
    });
    connect(m_rxAudio, &RxAudioChannel::audioBlockReady, this, &MainWindow::playAudioBlock);

    m_connection->connectToDevice(device, m_vfoPanel->frequencyHz());
    m_rxAudio->setMode(m_vfoPanel->rxMode());
    m_vfoPanel->setConnected(true);
    m_disconnectAction->setEnabled(true);
}

void MainWindow::playAudioBlock(const QVector<float> &interleavedStereo) {
    if (!interleavedStereo.isEmpty()) {
        double sumSq = 0.0;
        for (float v : interleavedStereo) {
            sumSq += double(v) * double(v);
        }
        // Crude, uncalibrated level - just enough to show the meter moving
        // with the actual received signal.
        const double rms = std::sqrt(sumSq / interleavedStereo.size());
        m_vfoPanel->setSignalLevel(rms / 4.0);
    }
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
    m_vfoPanel->setConnected(false);
    m_vfoPanel->setSignalLevel(0.0);
    m_disconnectAction->setEnabled(false);
    statusBar()->showMessage(tr("Disconnected."));
}
