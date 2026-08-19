#include "mainwindow.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QMenuBar>
#include <QMetaObject>
#include <QSettings>
#include <QStatusBar>
#include <QApplication>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>

#include "discoverydialog.h"
#include "filtertable.h"
#include "newprotocol.h"
#include "oldprotocol.h"
#include "panadapterwidget.h"
#include "rxaudio.h"
#include "spectrumanalyzer.h"
#include "toolbarwidget.h"
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
    // (panadapter above waterfall, sharing the frequency axis), then the
    // band/gain toolbar (deskHPSDR's "sliders" bar sits below the
    // receiver panel too). Zoom/pan isn't ported yet.
    m_vfoPanel = new VfoPanel(this);

    m_panadapter = new PanadapterWidget(this);
    m_waterfall = new WaterfallWidget(this);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_panadapter);
    splitter->addWidget(m_waterfall);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    m_toolbar = new ToolbarWidget(this);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_vfoPanel);
    layout->addWidget(splitter, /*stretch=*/1);
    layout->addWidget(m_toolbar);
    setCentralWidget(central);

    connect(m_vfoPanel, &VfoPanel::frequencyEditedHz, this, [this](double hz) { retuneTo(hz); });
    connect(m_panadapter, &PanadapterWidget::frequencyClicked, this, [this](double hz) { retuneTo(hz); });
    connect(m_vfoPanel, &VfoPanel::modeSelected, this, [this](RxMode mode) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, mode]() { m_rxAudio->setMode(mode); }, Qt::QueuedConnection);
        }
        // repopulateFilterCombo() (called from setRxMode(), which already
        // ran by the time this signal fires) picks the new mode's default
        // filter without emitting filterSelected() - sync the panadapter's
        // passband shading explicitly here instead of relying on that
        // signal for this particular case.
        const FilterEntry f = m_vfoPanel->currentFilter();
        m_panadapter->setPassband(f.low, f.high);
        updateBandStack();
    });
    connect(m_vfoPanel, &VfoPanel::filterSelected, this, [this](double lowHz, double highHz) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, lowHz, highHz]() { m_rxAudio->setPassband(lowHz, highHz); },
                Qt::QueuedConnection);
        }
        m_panadapter->setPassband(lowHz, highHz);
        updateBandStack();
    });
    connect(m_vfoPanel, &VfoPanel::attenuationChanged, this, [this](int db) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, db]() { m_connection->setAttenuation(db); }, Qt::QueuedConnection);
        }
    });
    connect(m_toolbar, &ToolbarWidget::bandSelected, this, [this](int bandIndex, double centerHz) {
        // Recall that band's last-used frequency/mode/filter if we have
        // one (see BandStackEntry/updateBandStack()); otherwise fall back
        // to the band center + a sensible band-plan-default mode, same as
        // before this existed. A plain typed/clicked/wheeled frequency
        // change (not a band switch) leaves the current mode alone.
        const auto it = m_bandStack.constFind(bandIndex);
        const bool haveEntry = it != m_bandStack.constEnd();
        const double hz = haveEntry ? it->frequencyHz : centerHz;
        const RxMode mode = haveEntry ? RxMode(it->mode) : defaultModeForFrequency(centerHz);

        // Mode/filter before retuneTo(): it calls updateBandStack() at the
        // end, which would otherwise capture the *old* mode/filter against
        // the *new* frequency and immediately clobber the entry we just
        // looked up.
        m_vfoPanel->setRxMode(mode);
        if (haveEntry) {
            m_vfoPanel->setFilterIndex(it->filterIndex);
        }
        retuneTo(hz);
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, mode]() { m_rxAudio->setMode(mode); }, Qt::QueuedConnection);
        }
        const FilterEntry f = m_vfoPanel->currentFilter();
        m_panadapter->setPassband(f.low, f.high);
        if (haveEntry && m_rxAudio) {
            // setMode() above already re-applied its own default filter
            // for the new mode - override with the specific one we saved.
            QMetaObject::invokeMethod(
                m_rxAudio, [this, f]() { m_rxAudio->setPassband(f.low, f.high); }, Qt::QueuedConnection);
        }
    });
    connect(m_toolbar, &ToolbarWidget::afGainChanged, this, [this](double dB) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, dB]() { m_rxAudio->setAfGain(dB); }, Qt::QueuedConnection);
        }
    });
    connect(m_toolbar, &ToolbarWidget::agcModeChanged, this, [this](AgcMode mode) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, mode]() { m_rxAudio->setAgcMode(mode); }, Qt::QueuedConnection);
        }
    });
    connect(m_toolbar, &ToolbarWidget::agcTopChanged, this, [this](double dB) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, dB]() { m_rxAudio->setAgcTop(dB); }, Qt::QueuedConnection);
        }
    });
    connect(m_toolbar, &ToolbarWidget::noiseBlankerChanged, this, [this](NoiseBlankerMode mode) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, mode]() { m_rxAudio->setNoiseBlanker(mode); }, Qt::QueuedConnection);
        }
        m_vfoPanel->setNoiseBlankerLabel(m_toolbar->currentNoiseBlankerLabel());
    });
    connect(m_toolbar, &ToolbarWidget::noiseReductionChanged, this, [this](NoiseReductionMode mode) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, mode]() { m_rxAudio->setNoiseReduction(mode); }, Qt::QueuedConnection);
        }
    });
    connect(m_toolbar, &ToolbarWidget::autoNotchChanged, this, [this](bool enabled) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, enabled]() { m_rxAudio->setAutoNotch(enabled); }, Qt::QueuedConnection);
        }
    });
    connect(m_toolbar, &ToolbarWidget::spectralNoiseBlankerChanged, this, [this](bool enabled) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, enabled]() { m_rxAudio->setSpectralNoiseBlanker(enabled); }, Qt::QueuedConnection);
        }
    });
    connect(m_toolbar, &ToolbarWidget::nr4SmoothingChanged, this, [this](double percent) {
        if (m_rxAudio) {
            QMetaObject::invokeMethod(
                m_rxAudio, [this, percent]() { m_rxAudio->setNr4SmoothingFactor(percent); }, Qt::QueuedConnection);
        }
    });
    connect(m_toolbar, &ToolbarWidget::filterBoardEnabledChanged, this, [this](bool enabled) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, enabled]() { m_connection->setFilterBoardEnabled(enabled); },
                Qt::QueuedConnection);
        }
    });
    connect(m_toolbar, &ToolbarWidget::widebandEnabledChanged, this, [this](bool enabled) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, enabled]() { m_connection->setWidebandEnabled(enabled); },
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
    loadSettings();
    m_panadapter->setCenterFrequencyHz(m_vfoPanel->frequencyHz());
    m_panadapter->setSampleRateHz(48000.0);
    m_toolbar->setFrequencyHz(m_vfoPanel->frequencyHz());
    m_vfoPanel->setBandLabel(m_toolbar->currentBandLabel());
    m_vfoPanel->setNoiseBlankerLabel(m_toolbar->currentNoiseBlankerLabel());
    {
        const FilterEntry f = m_vfoPanel->currentFilter();
        m_panadapter->setPassband(f.low, f.high);
    }

    // Painting on every incoming spectrum frame (~23/s) made each frame's
    // handler expensive enough (grid+trace redraw, waterfall image scroll,
    // widget repaint) to noticeably starve the GUI event loop. A repaint
    // timer, rate user-adjustable via the toolbar's FPS combo (default
    // 20fps), bounds that cost regardless of how fast frames actually
    // arrive.
    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setInterval(1000 / m_toolbar->fps());
    connect(m_repaintTimer, &QTimer::timeout, this, &MainWindow::repaintDisplays);
    connect(m_toolbar, &ToolbarWidget::fpsChanged, this,
            [this](int fps) { m_repaintTimer->setInterval(1000 / qMax(1, fps)); });
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
    if (m_toolbar->widebandEnabled()) {
        // The wideband full-band sweep (RadioConnection::wideSpectrumReady,
        // connected in connectToDevice()) drives the panadapter/waterfall
        // directly while this is on - don't also overwrite them with the
        // narrow DDC-based feed below.
        return;
    }

    // Zoom: show only the center slice of the full received span, stretched
    // across the same widget width - see ToolbarWidget::zoomFactor(). Both
    // widgets stay entirely zoom-unaware: they just draw whatever spectrum
    // array + sample rate they're given, so cropping here is the only
    // change needed.
    const int zoom = qBound(1, m_toolbar->zoomFactor(), m_latestSpectrum.isEmpty() ? 1 : m_latestSpectrum.size());
    QVector<float> displaySpectrum = m_latestSpectrum;
    double displaySampleRateHz = 48000.0;
    if (zoom > 1 && !displaySpectrum.isEmpty()) {
        const int n = displaySpectrum.size();
        const int cropped = std::max(1, n / zoom);
        const int start = (n - cropped) / 2;
        displaySpectrum = displaySpectrum.mid(start, cropped);
        displaySampleRateHz = 48000.0 / zoom;
    }

    // Auto-scale the displayed dB range to the actual incoming data
    // instead of a fixed guess: real receive levels vary a lot with
    // antenna/band/AGC settings, and a fixed range that doesn't match
    // what's actually there just looks like flat "mush" with no visible
    // contrast, even though the underlying spectrum is fine. Smoothed
    // (not snapped per-frame) so the display doesn't jitter. Based on
    // what's actually visible (the zoomed slice), not the full band.
    if (!displaySpectrum.isEmpty()) {
        float frameMin = displaySpectrum[0];
        float frameMax = displaySpectrum[0];
        for (float v : displaySpectrum) {
            frameMin = std::min(frameMin, v);
            frameMax = std::max(frameMax, v);
        }
        constexpr float kAlpha = 0.08f;
        m_dbFloor += kAlpha * ((frameMin - 5.0f) - m_dbFloor);
        m_dbCeil += kAlpha * ((frameMax + 8.0f) - m_dbCeil);
        if (m_dbCeil - m_dbFloor < 20.0f) {
            m_dbCeil = m_dbFloor + 20.0f;
        }

        // The waterfall's color mapping needs its own, differently-anchored
        // range from the panadapter's: raw min/max (above) makes a single-
        // frame FFT bin's noise (itself spiky - a few dB of per-bin
        // variance even for flat noise, no averaging here) dominate the
        // middle of the gradient, so the whole waterfall looked green with
        // no contrast against real signals. Anchor to the *median* bin
        // power instead (a stable noise-floor estimate, unlike min/max)
        // and use a fixed span above it - background noise then sits in
        // the gradient's black/blue end regardless of the band's absolute
        // level, and only bins meaningfully stronger than the noise floor
        // reach green/yellow/red.
        QVector<float> sorted = displaySpectrum;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const float noiseFloor = sorted[sorted.size() / 2];
        constexpr float kWaterfallSpanDb = 30.0f;
        const float targetFloor = noiseFloor - 4.0f;
        const float targetCeil = std::max(noiseFloor + kWaterfallSpanDb, frameMax + 3.0f);
        m_waterfallFloor += kAlpha * (targetFloor - m_waterfallFloor);
        m_waterfallCeil += kAlpha * (targetCeil - m_waterfallCeil);
        if (m_waterfallCeil - m_waterfallFloor < 15.0f) {
            m_waterfallCeil = m_waterfallFloor + 15.0f;
        }
    }
    m_panadapter->setDbRange(m_dbFloor, m_dbCeil);
    m_waterfall->setDbRange(m_waterfallFloor, m_waterfallCeil);
    m_panadapter->setSampleRateHz(displaySampleRateHz);
    m_panadapter->setSpectrum(displaySpectrum);
    m_waterfall->pushSpectrum(displaySpectrum);
}

MainWindow::~MainWindow() {
    saveSettings();
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

void MainWindow::saveSettings() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("vfo"));
    settings.setValue(QStringLiteral("frequencyHz"), m_vfoPanel->frequencyHz());
    settings.setValue(QStringLiteral("mode"), int(m_vfoPanel->rxMode()));
    settings.setValue(QStringLiteral("filterIndex"), m_vfoPanel->currentFilterIndex());
    settings.setValue(QStringLiteral("stepIndex"), m_vfoPanel->currentStepIndex());
    settings.setValue(QStringLiteral("attenuationDb"), m_vfoPanel->attenuationDb());
    settings.setValue(QStringLiteral("afGainDb"), m_toolbar->afGainDb());
    settings.setValue(QStringLiteral("rfGainDb"), m_toolbar->rfGainDb());
    settings.setValue(QStringLiteral("zoomFactor"), m_toolbar->zoomFactor());
    settings.setValue(QStringLiteral("fps"), m_toolbar->fps());
    settings.setValue(QStringLiteral("agcMode"), int(m_toolbar->agcMode()));
    settings.setValue(QStringLiteral("agcTopDb"), m_toolbar->agcTopDb());
    settings.setValue(QStringLiteral("noiseBlankerMode"), int(m_toolbar->noiseBlankerMode()));
    settings.setValue(QStringLiteral("noiseReductionMode"), int(m_toolbar->noiseReductionMode()));
    settings.setValue(QStringLiteral("autoNotchEnabled"), m_toolbar->autoNotchEnabled());
    settings.setValue(QStringLiteral("spectralNoiseBlankerEnabled"), m_toolbar->spectralNoiseBlankerEnabled());
    settings.setValue(QStringLiteral("nr4SmoothingFactor"), m_toolbar->nr4SmoothingFactor());
    settings.setValue(QStringLiteral("filterBoardEnabled"), m_toolbar->filterBoardEnabled());
    settings.setValue(QStringLiteral("widebandEnabled"), m_toolbar->widebandEnabled());
    settings.endGroup();

    settings.beginWriteArray(QStringLiteral("bandstack"));
    int arrayIndex = 0;
    for (auto it = m_bandStack.constBegin(); it != m_bandStack.constEnd(); ++it) {
        settings.setArrayIndex(arrayIndex++);
        settings.setValue(QStringLiteral("band"), it.key());
        settings.setValue(QStringLiteral("frequencyHz"), it->frequencyHz);
        settings.setValue(QStringLiteral("mode"), it->mode);
        settings.setValue(QStringLiteral("filterIndex"), it->filterIndex);
    }
    settings.endArray();
}

void MainWindow::loadSettings() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("vfo"));
    if (!settings.contains(QStringLiteral("frequencyHz"))) {
        settings.endGroup();
        return; // First run - nothing saved yet, keep the built-in defaults.
    }
    m_vfoPanel->setFrequencyHz(settings.value(QStringLiteral("frequencyHz")).toDouble());
    // Mode first: setRxMode() repopulates the filter combo to that mode's
    // default list/selection, which the saved filterIndex then overrides.
    m_vfoPanel->setRxMode(RxMode(settings.value(QStringLiteral("mode")).toInt()));
    m_vfoPanel->setFilterIndex(settings.value(QStringLiteral("filterIndex")).toInt());
    m_vfoPanel->setStepIndex(settings.value(QStringLiteral("stepIndex")).toInt());
    m_vfoPanel->setAttenuationDb(settings.value(QStringLiteral("attenuationDb")).toInt());
    m_toolbar->setAfGainDb(settings.value(QStringLiteral("afGainDb")).toDouble());
    m_toolbar->setRfGainDb(settings.value(QStringLiteral("rfGainDb")).toDouble());
    m_toolbar->setZoomFactor(settings.value(QStringLiteral("zoomFactor"), 1).toInt());
    m_toolbar->setFps(settings.value(QStringLiteral("fps"), 20).toInt());
    m_toolbar->setAgcMode(AgcMode(settings.value(QStringLiteral("agcMode"), int(AgcMode::Medium)).toInt()));
    m_toolbar->setAgcTopDb(settings.value(QStringLiteral("agcTopDb"), 80.0).toDouble());
    m_toolbar->setNoiseBlankerMode(
        NoiseBlankerMode(settings.value(QStringLiteral("noiseBlankerMode"), int(NoiseBlankerMode::Off)).toInt()));
    m_toolbar->setNoiseReductionMode(NoiseReductionMode(
        settings.value(QStringLiteral("noiseReductionMode"), int(NoiseReductionMode::Off)).toInt()));
    m_toolbar->setAutoNotchEnabled(settings.value(QStringLiteral("autoNotchEnabled"), false).toBool());
    m_toolbar->setSpectralNoiseBlankerEnabled(
        settings.value(QStringLiteral("spectralNoiseBlankerEnabled"), false).toBool());
    m_toolbar->setNr4SmoothingFactor(settings.value(QStringLiteral("nr4SmoothingFactor"), 0.0).toDouble());
    m_toolbar->setFilterBoardEnabled(settings.value(QStringLiteral("filterBoardEnabled"), false).toBool());
    m_toolbar->setWidebandEnabled(settings.value(QStringLiteral("widebandEnabled"), false).toBool());
    settings.endGroup();

    m_bandStack.clear();
    const int bandStackCount = settings.beginReadArray(QStringLiteral("bandstack"));
    for (int i = 0; i < bandStackCount; ++i) {
        settings.setArrayIndex(i);
        const int band = settings.value(QStringLiteral("band")).toInt();
        BandStackEntry entry;
        entry.frequencyHz = settings.value(QStringLiteral("frequencyHz")).toDouble();
        entry.mode = settings.value(QStringLiteral("mode")).toInt();
        entry.filterIndex = settings.value(QStringLiteral("filterIndex")).toInt();
        m_bandStack[band] = entry;
    }
    settings.endArray();
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
    connectToDevice(device);
}

void MainWindow::connectToDevice(const DiscoveredDevice &device) {
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
        // finishOpen() applies a hardcoded AM baseline before m_open flips
        // true (see rxaudio.cpp) - re-apply the VFO panel's actual mode
        // now that the channel is guaranteed open, rather than racing it
        // right after open() like this used to (setMode() silently no-ops
        // while m_open is still false, so it could lose to that baseline
        // depending on how long OpenChannel()/FFTW planning took).
        QMetaObject::invokeMethod(
            m_rxAudio, [this]() { m_rxAudio->setMode(m_vfoPanel->rxMode()); }, Qt::QueuedConnection);
        // Same race, same fix, for whatever AGC mode/gain and noise
        // blanker setting the toolbar already holds (e.g. restored from
        // saved settings) - finishOpen() only applies its own AGC_MEDIUM/
        // NB-off baseline.
        const AgcMode agcMode = m_toolbar->agcMode();
        const double agcTop = m_toolbar->agcTopDb();
        const NoiseBlankerMode nbMode = m_toolbar->noiseBlankerMode();
        const NoiseReductionMode nrMode = m_toolbar->noiseReductionMode();
        const bool anfEnabled = m_toolbar->autoNotchEnabled();
        const bool snbEnabled = m_toolbar->spectralNoiseBlankerEnabled();
        const double nr4Smoothing = m_toolbar->nr4SmoothingFactor();
        QMetaObject::invokeMethod(
            m_rxAudio,
            [this, agcMode, agcTop, nbMode, nrMode, anfEnabled, snbEnabled, nr4Smoothing]() {
                m_rxAudio->setAgcMode(agcMode);
                m_rxAudio->setAgcTop(agcTop);
                m_rxAudio->setNoiseBlanker(nbMode);
                // Before setNoiseReduction(): if nrMode is Sbnr, its branch
                // reads the smoothing value already stored on m_rxAudio.
                m_rxAudio->setNr4SmoothingFactor(nr4Smoothing);
                m_rxAudio->setNoiseReduction(nrMode);
                m_rxAudio->setAutoNotch(anfEnabled);
                m_rxAudio->setSpectralNoiseBlanker(snbEnabled);
            },
            Qt::QueuedConnection);
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
    connect(m_rxAudio, &RxAudioChannel::meterUpdated, this, [this](double dbm) {
        // Compensate for whatever ADC0 attenuation is currently dialed in,
        // so raising it to fight front-end overload doesn't make the
        // meter falsely read a weaker signal - see VfoPanel::attenuationDb().
        // Also fold in the toolbar's RF gain calibration offset - see
        // ToolbarWidget::rfGainDb(). Subtracted, not added: matches
        // core/deskhpsdr-src/receiver.c's rx_update_display() formula
        // ("level += calib + attenuation - adc[rx->adc].gain") - more RF
        // gain means the same real signal reads hotter internally, so it
        // has to come back out to stay calibrated.
        m_vfoPanel->setSignalDbm(dbm + m_vfoPanel->attenuationDb() - m_toolbar->rfGainDb());
    });
    QMetaObject::invokeMethod(
        m_rxAudio,
        [this]() {
            m_audioDevice = m_audioSink->start();
            m_rxAudio->open(/*channel=*/0, /*inputSampleRate=*/48000);
        },
        Qt::QueuedConnection);
    statusBar()->showMessage(tr("Opening audio engine..."));

    // Which wire protocol a device speaks only changes which concrete
    // RadioConnection subclass gets constructed here - everything else
    // (signal wiring, thread affinity, audio/spectrum feed) is identical
    // since both implement the same RadioConnection interface.
    if (device.protocol == NEW_PROTOCOL) {
        m_connection = new NewProtocolConnection();
    } else {
        m_connection = new OldProtocolConnection();
    }
    m_connection->moveToThread(m_workerThread);
    connect(m_connection, &RadioConnection::statsUpdated, this,
            [this, device](quint64 packets, quint64 samples, double sps) {
                Q_UNUSED(packets);
                Q_UNUSED(samples);
                statusBar()->showMessage(
                    tr("Connected to %1 at %2 - %3 samples/s").arg(device.name, device.address.toString()).arg(sps, 0, 'f', 0));
            });
    connect(m_connection, &RadioConnection::errorOccurred, this, [this](const QString &msg) {
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
    connect(m_connection, &RadioConnection::iqSamplesReady, m_rxAudio, [this](const QVector<double> &iq) {
        for (int i = 0; i + 1 < iq.size(); i += 2) {
            m_rxAudio->feedSample(iq[i], iq[i + 1]);
        }
    });
    connect(m_connection, &RadioConnection::iqSamplesReady, m_spectrum, [this](const QVector<double> &iq) {
        for (int i = 0; i + 1 < iq.size(); i += 2) {
            m_spectrum->feedSample(iq[i], iq[i + 1]);
        }
    });
    // GUI-thread context (`this`) - Qt auto-queues this across threads,
    // same as statsUpdated/errorOccurred above. Only fires while
    // ToolbarWidget::widebandEnabled() is on (NewProtocolConnection only
    // emits it then); repaintDisplays() skips its own narrow-band feed
    // while that's the case, so the two don't fight over the same widgets.
    connect(m_connection, &RadioConnection::wideSpectrumReady, this,
            [this](const QVector<float> &magnitudesDb, double sampleRateHz) {
                if (magnitudesDb.isEmpty()) {
                    return;
                }
                float frameMin = magnitudesDb[0];
                float frameMax = magnitudesDb[0];
                for (float v : magnitudesDb) {
                    frameMin = std::min(frameMin, v);
                    frameMax = std::max(frameMax, v);
                }
                m_panadapter->setDbRange(frameMin - 5.0f, frameMax + 8.0f);
                m_waterfall->setDbRange(frameMin - 5.0f, frameMax + 8.0f);
                m_panadapter->setCenterFrequencyHz(sampleRateHz / 2.0);
                m_panadapter->setSampleRateHz(sampleRateHz);
                m_panadapter->setSpectrum(magnitudesDb);
                m_waterfall->pushSpectrum(magnitudesDb);
            });

    QMetaObject::invokeMethod(
        m_connection, [this, device]() { m_connection->connectToDevice(device, m_vfoPanel->frequencyHz()); },
        Qt::QueuedConnection);
    // A fresh connection object always starts with filterBoardEnabled/
    // widebandEnabled=false internally - re-push whatever the toolbar
    // currently holds (e.g. restored from saved settings) rather than
    // silently resetting it.
    {
        const bool filterBoardEnabled = m_toolbar->filterBoardEnabled();
        QMetaObject::invokeMethod(
            m_connection, [this, filterBoardEnabled]() { m_connection->setFilterBoardEnabled(filterBoardEnabled); },
            Qt::QueuedConnection);
        const bool widebandEnabled = m_toolbar->widebandEnabled();
        QMetaObject::invokeMethod(
            m_connection, [this, widebandEnabled]() { m_connection->setWidebandEnabled(widebandEnabled); },
            Qt::QueuedConnection);
    }
    m_vfoPanel->setConnected(true);
    m_toolbar->setConnected(true);
    {
        const FilterEntry f = m_vfoPanel->currentFilter();
        m_panadapter->setPassband(f.low, f.high);
    }
    m_disconnectAction->setEnabled(true);
}

void MainWindow::playAudioBlock(const QVector<float> &interleavedStereo) {
    if (!m_audioDevice) {
        return;
    }
    m_audioDevice->write(reinterpret_cast<const char *>(interleavedStereo.constData()),
                          interleavedStereo.size() * qint64(sizeof(float)));
}

void MainWindow::retuneTo(double hz) {
    m_vfoPanel->setFrequencyHz(hz);
    if (m_connection) {
        QMetaObject::invokeMethod(
            m_connection, [this, hz]() { m_connection->setRxFrequency(hz); }, Qt::QueuedConnection);
    }
    m_panadapter->setCenterFrequencyHz(hz);
    m_toolbar->setFrequencyHz(hz);
    m_vfoPanel->setBandLabel(m_toolbar->currentBandLabel());
    updateBandStack();
}

void MainWindow::updateBandStack() {
    const int bandIndex = m_toolbar->bandIndexForFrequency(m_vfoPanel->frequencyHz());
    if (bandIndex < 0) {
        return;
    }
    m_bandStack[bandIndex] = {m_vfoPanel->frequencyHz(), int(m_vfoPanel->rxMode()), m_vfoPanel->currentFilterIndex()};
}

void MainWindow::disconnectFromRadio() {
    if (m_connection) {
        // Both calls run on the worker thread that owns these objects
        // (see the class comment in mainwindow.h); deleteLater() is
        // itself thread-safe to call from anywhere, but
        // disconnectFromDevice()/close() aren't, so they're queued too -
        // in the same invocation, so ordering is preserved.
        RadioConnection *connection = m_connection;
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
    m_toolbar->setConnected(false);
    m_vfoPanel->setSignalDbm(-140.0);
    m_panadapter->setPassband(0.0, 0.0);
    m_disconnectAction->setEnabled(false);
    statusBar()->showMessage(tr("Disconnected."));
}
