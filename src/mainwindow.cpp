#include "mainwindow.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QMenuBar>
#include <QMetaObject>
#include <QStatusBar>
#include <QApplication>
#include <QSplitter>
#include <QThread>
#include <QTimer>
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
            QMetaObject::invokeMethod(
                m_connection, [this, hz]() { m_connection->setRxFrequency(hz); }, Qt::QueuedConnection);
        }
        m_panadapter->setCenterFrequencyHz(hz);
    });
    connect(m_vfoPanel, &VfoPanel::modeSelected, this, [this](RxMode mode) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, mode]() { m_rxAudio->setMode(mode); }, Qt::QueuedConnection);
        }
    });
    connect(m_vfoPanel, &VfoPanel::filterSelected, this, [this](double lowHz, double highHz) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, lowHz, highHz]() { m_rxAudio->setPassband(lowHz, highHz); },
                Qt::QueuedConnection);
        }
    });

    m_spectrum = new SpectrumAnalyzer(this);
    connect(m_spectrum, &SpectrumAnalyzer::spectrumReady, this, [this](const QVector<float> &db) {
        // Just store it - painting happens on m_repaintTimer's own fixed
        // schedule (see below and repaintDisplays()), decoupled from
        // whatever rate frames actually arrive at.
        m_latestSpectrum = db;
        m_spectrumDirty = true;
    });
    m_panadapter->setCenterFrequencyHz(m_vfoPanel->frequencyHz());
    m_panadapter->setSampleRateHz(48000.0);

    // Painting on every incoming spectrum frame (~23/s) made each frame's
    // handler expensive enough (grid+trace redraw, waterfall image scroll,
    // widget repaint) to noticeably starve the GUI event loop. A fixed
    // 20 fps repaint timer bounds that cost regardless of how fast frames
    // actually arrive.
    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setInterval(50);
    connect(m_repaintTimer, &QTimer::timeout, this, &MainWindow::repaintDisplays);
    m_repaintTimer->start();

    m_workerThread = new QThread(this);
    m_workerThread->setObjectName("QhpsdrWorker");
    m_workerThread->start();

    // The panadapter FFT (m_spectrum) is independent of WDSP audio demod
    // (m_workerThread) - both just read the same raw I/Q, neither depends
    // on the other's output - so it gets its own thread/core rather than
    // sharing one.
    m_spectrumThread = new QThread(this);
    m_spectrumThread->setObjectName("QhpsdrSpectrum");
    m_spectrumThread->start();

    statusBar()->showMessage(tr("Qhpsdr Ready."));
}

void MainWindow::repaintDisplays() {
    if (!m_spectrumDirty) {
        return;
    }
    m_spectrumDirty = false;

    // Auto-scale the displayed dB range to the actual incoming data
    // instead of a fixed guess: real receive levels vary a lot with
    // antenna/band/AGC settings, and a fixed range that doesn't match
    // what's actually there just looks like flat "mush" with no visible
    // contrast, even though the underlying spectrum is fine. Smoothed
    // (not snapped per-frame) so the display doesn't jitter.
    if (!m_latestSpectrum.isEmpty()) {
        float frameMin = m_latestSpectrum[0];
        float frameMax = m_latestSpectrum[0];
        for (float v : m_latestSpectrum) {
            frameMin = std::min(frameMin, v);
            frameMax = std::max(frameMax, v);
        }
        constexpr float kAlpha = 0.08f;
        m_dbFloor += kAlpha * ((frameMin - 5.0f) - m_dbFloor);
        m_dbCeil += kAlpha * ((frameMax + 8.0f) - m_dbCeil);
        if (m_dbCeil - m_dbFloor < 20.0f) {
            m_dbCeil = m_dbFloor + 20.0f;
        }
    }
    m_panadapter->setDbRange(m_dbFloor, m_dbCeil);
    m_waterfall->setDbRange(m_dbFloor, m_dbCeil);
    m_panadapter->setSpectrum(m_latestSpectrum);
    m_waterfall->pushSpectrum(m_latestSpectrum);
}

MainWindow::~MainWindow() {
    disconnectFromRadio();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
    if (m_spectrumThread) {
        m_spectrumThread->quit();
        m_spectrumThread->wait();
    }
}

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

    // No parent on any of these four: QObject::moveToThread() refuses
    // objects that have one. m_spectrum was already parented to `this`
    // earlier in the constructor (it's created once, up front, unlike the
    // others which get recreated per connection) - move it along too. It
    // goes to m_spectrumThread, not m_workerThread - see the class
    // comment in mainwindow.h.
    m_spectrum->setParent(nullptr);
    m_spectrum->moveToThread(m_spectrumThread);

    // QAudioSink also goes on m_workerThread rather than staying on the
    // GUI thread: write() can block waiting for buffer space, and doing
    // that from a GUI-thread slot (as a queued response to
    // audioBlockReady) stalled the event loop just as badly as the DSP
    // work itself did before that got moved off. It's created here,
    // freshly, instead of a QObject subclass constructor, so there's
    // nowhere upstream that could accidentally parent it to `this` again.
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), format);
    m_audioSink->moveToThread(m_workerThread);

    m_rxAudio = new RxAudioChannel();
    m_rxAudio->moveToThread(m_workerThread);
    connect(m_rxAudio, &RxAudioChannel::opened, this, [this]() {
        statusBar()->showMessage(tr("Audio engine ready."));
    });
    connect(m_rxAudio, &RxAudioChannel::buildingWisdom, this, [this]() {
        statusBar()->showMessage(
            tr("Building FFT cache (one-time, can take several minutes) - audio starts once this finishes..."));
    });
    // Direct (same-thread) connection: the actual audio write happens
    // right there on the worker thread, not bounced through the GUI
    // thread. The VFO meter still needs the GUI thread (it's a widget),
    // so that's a second, separate connection below with `this` as
    // context - cheap to marshal (one QVector<float> copy), unlike a
    // blocking QAudioSink::write() call would have been.
    connect(m_rxAudio, &RxAudioChannel::audioBlockReady, m_rxAudio,
            [this](const QVector<float> &block) { playAudioBlock(block); });
    connect(m_rxAudio, &RxAudioChannel::audioBlockReady, this, &MainWindow::updateSignalMeter);
    QMetaObject::invokeMethod(
        m_rxAudio,
        [this]() {
            m_audioDevice = m_audioSink->start();
            m_rxAudio->open(/*channel=*/0, /*inputSampleRate=*/48000);
        },
        Qt::QueuedConnection);
    statusBar()->showMessage(tr("Opening audio engine..."));

    m_connection = new OldProtocolConnection();
    m_connection->moveToThread(m_workerThread);
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
    // Two separate connections, not one lambda doing both feeds: context
    // m_rxAudio (worker-thread-affine) makes the first a same-thread
    // direct connection since the emitter (m_connection) is also on
    // m_workerThread, so WDSP demod work happens right there. Context
    // m_spectrum is on the *different* m_spectrumThread, so that one is a
    // genuinely separate, automatically-queued cross-thread delivery -
    // the FFT runs concurrently with audio demod instead of competing for
    // the same thread's time. Neither bounces to the GUI thread just
    // because the lambdas were written with `this` captured for other
    // members - the connection's context object is what determines which
    // thread actually runs it, not what the lambda captures.
    connect(m_connection, &OldProtocolConnection::iqSamplesReady, m_rxAudio, [this](const QVector<double> &iq) {
        for (int i = 0; i + 1 < iq.size(); i += 2) {
            m_rxAudio->feedSample(iq[i], iq[i + 1]);
        }
    });
    connect(m_connection, &OldProtocolConnection::iqSamplesReady, m_spectrum, [this](const QVector<double> &iq) {
        for (int i = 0; i + 1 < iq.size(); i += 2) {
            m_spectrum->feedSample(iq[i], iq[i + 1]);
        }
    });

    QMetaObject::invokeMethod(
        m_connection, [this, device]() { m_connection->connectToDevice(device, m_vfoPanel->frequencyHz()); },
        Qt::QueuedConnection);
    QMetaObject::invokeMethod(
        m_rxAudio, [this]() { m_rxAudio->setMode(m_vfoPanel->rxMode()); }, Qt::QueuedConnection);
    m_vfoPanel->setConnected(true);
    m_disconnectAction->setEnabled(true);
}

void MainWindow::playAudioBlock(const QVector<float> &interleavedStereo) {
    if (!m_audioDevice) {
        return;
    }
    m_audioDevice->write(reinterpret_cast<const char *>(interleavedStereo.constData()),
                          interleavedStereo.size() * qint64(sizeof(float)));
}

void MainWindow::updateSignalMeter(const QVector<float> &interleavedStereo) {
    if (interleavedStereo.isEmpty()) {
        return;
    }
    double sumSq = 0.0;
    for (float v : interleavedStereo) {
        sumSq += double(v) * double(v);
    }
    // Crude, uncalibrated level - just enough to show the meter moving
    // with the actual received signal.
    const double rms = std::sqrt(sumSq / interleavedStereo.size());
    m_vfoPanel->setSignalLevel(rms / 4.0);
}

void MainWindow::disconnectFromRadio() {
    if (m_connection) {
        // Both calls run on the worker thread that owns these objects
        // (see the class comment in mainwindow.h); deleteLater() is
        // itself thread-safe to call from anywhere, but
        // disconnectFromDevice()/close() aren't, so they're queued too -
        // in the same invocation, so ordering is preserved.
        OldProtocolConnection *connection = m_connection;
        QMetaObject::invokeMethod(
            connection,
            [connection]() {
                connection->disconnectFromDevice();
                connection->deleteLater();
            },
            Qt::QueuedConnection);
        m_connection = nullptr;
    }
    if (m_rxAudio) {
        RxAudioChannel *rxAudio = m_rxAudio;
        QMetaObject::invokeMethod(
            rxAudio,
            [rxAudio]() {
                rxAudio->close();
                rxAudio->deleteLater();
            },
            Qt::QueuedConnection);
        m_rxAudio = nullptr;
    }
    if (m_audioSink) {
        QAudioSink *audioSink = m_audioSink;
        QMetaObject::invokeMethod(
            audioSink,
            [audioSink]() {
                audioSink->stop();
                audioSink->deleteLater();
            },
            Qt::QueuedConnection);
        m_audioSink = nullptr;
        m_audioDevice = nullptr;
    }
    m_vfoPanel->setConnected(false);
    m_vfoPanel->setSignalLevel(0.0);
    m_disconnectAction->setEnabled(false);
    statusBar()->showMessage(tr("Disconnected."));
}
