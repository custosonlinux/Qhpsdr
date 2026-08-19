#include "receiverpanel.h"

#include <QMetaObject>
#include <QSettings>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "devicepanel.h"
#include "filtertable.h"
#include "panadapterwidget.h"
#include "radioconnection.h"
#include "rxaudio.h"
#include "spectrumanalyzer.h"
#include "toolbarwidget.h"
#include "vfopanel.h"
#include "waterfallwidget.h"

ReceiverPanel::ReceiverPanel(int channelIndex, const QString &label, QWidget *parent)
    : QWidget(parent), m_channelIndex(channelIndex) {
    setWindowTitle(label);

    m_vfoPanel = new VfoPanel(this);
    m_panadapter = new PanadapterWidget(this);
    m_waterfall = new WaterfallWidget(this);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_panadapter);
    splitter->addWidget(m_waterfall);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    m_toolbar = new ToolbarWidget(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_vfoPanel);
    layout->addWidget(splitter, /*stretch=*/1);
    layout->addWidget(m_toolbar);

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
    connect(m_toolbar, &ToolbarWidget::bandSelected, this, [this](int bandIndex, double centerHz) {
        // Recall that band's last-used frequency/mode/filter if we have
        // one (see BandStackEntry/updateBandStack()); otherwise fall back
        // to the band center + a sensible band-plan-default mode.
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
    connect(m_toolbar, &ToolbarWidget::sampleRateChanged, this, [this](int hz) {
        m_panadapter->setSampleRateHz(double(hz));
        emit sampleRateChanged(hz);
        if (m_rxAudio && m_connection) {
            // WDSP bakes the input rate in at OpenChannel() time - a live
            // rate change needs a fresh open, not just a Set* call.
            // Remembered context from the last startAudio() call lets this
            // reopen without MainWindow having to redo any wiring.
            QThread *workerThread = m_workerThread;
            RadioConnection *connection = m_connection;
            const bool useSecondDdc = m_useSecondDdc;
            stopAudio();
            startAudio(workerThread, hz, connection, useSecondDdc);
        }
    });

    m_spectrum = new SpectrumAnalyzer();
    // Own dedicated thread, not shared with the other ReceiverPanel or the
    // WDSP worker thread - both are independent consumers of the same raw
    // I/Q stream, so each gets its own core rather than serializing (see
    // [[feedback_parallelism]]).
    m_spectrumThread = new QThread(this);
    m_spectrumThread->setObjectName(QStringLiteral("QhpsdrSpectrum%1").arg(channelIndex));
    m_spectrum->moveToThread(m_spectrumThread);
    m_spectrumThread->start();
    connect(m_spectrum, &SpectrumAnalyzer::spectrumReady, this, [this](const QVector<float> &db) {
        m_latestSpectrum = db;
        m_spectrumDirty = true;
    });

    m_panadapter->setCenterFrequencyHz(m_vfoPanel->frequencyHz());
    m_panadapter->setSampleRateHz(double(m_toolbar->sampleRateHz()));
    m_toolbar->setFrequencyHz(m_vfoPanel->frequencyHz());
    m_vfoPanel->setBandLabel(m_toolbar->currentBandLabel());
    m_vfoPanel->setNoiseBlankerLabel(m_toolbar->currentNoiseBlankerLabel());
    {
        const FilterEntry f = m_vfoPanel->currentFilter();
        m_panadapter->setPassband(f.low, f.high);
    }

    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setInterval(1000 / m_toolbar->fps());
    connect(m_repaintTimer, &QTimer::timeout, this, &ReceiverPanel::repaintDisplays);
    connect(m_toolbar, &ToolbarWidget::fpsChanged, this,
            [this](int fps) { m_repaintTimer->setInterval(1000 / qMax(1, fps)); });
    m_repaintTimer->start();

    setConnected(false);
}

ReceiverPanel::~ReceiverPanel() {
    stopAudio();
    if (m_spectrumThread) {
        m_spectrumThread->quit();
        m_spectrumThread->wait();
    }
    delete m_spectrum;
}

void ReceiverPanel::setDevicePanel(DevicePanel *devicePanel) { m_devicePanel = devicePanel; }

void ReceiverPanel::startAudio(QThread *workerThread, int inputSampleRate, RadioConnection *connection,
                                bool useSecondDdc) {
    m_workerThread = workerThread;
    m_connection = connection;
    m_useSecondDdc = useSecondDdc;

    m_rxAudio = new RxAudioChannel();
    m_rxAudio->moveToThread(workerThread);

    connect(m_rxAudio, &RxAudioChannel::opened, this, [this]() {
        emit statusMessage(tr("Audio engine ready."));
        // finishOpen() applies a hardcoded AM baseline before m_open flips
        // true (see rxaudio.cpp) - re-apply the VFO panel's actual mode
        // now that the channel is guaranteed open, rather than racing it.
        QMetaObject::invokeMethod(
            m_rxAudio, [this]() { m_rxAudio->setMode(m_vfoPanel->rxMode()); }, Qt::QueuedConnection);
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
        emit statusMessage(
            tr("Building FFT cache (one-time, can take several minutes) - audio starts once this finishes..."));
    });
    // Direct (same-thread) connection: the actual mixing/emission happens
    // right there on the worker thread - see ReceiverPanel::audioBlockReady().
    connect(m_rxAudio, &RxAudioChannel::audioBlockReady, m_rxAudio,
            [this](const QVector<float> &block) { emit audioBlockReady(block); });
    connect(m_rxAudio, &RxAudioChannel::meterUpdated, this, [this](double dbm) {
        // Compensate for whatever ADC0 attenuation/RF-gain calibration is
        // currently dialed in on the shared DevicePanel, so raising
        // attenuation to fight front-end overload doesn't make the meter
        // falsely read a weaker signal - matches core/deskhpsdr-src/
        // receiver.c's rx_update_display() formula ("level += calib +
        // attenuation - adc[rx->adc].gain").
        const int attenDb = m_devicePanel ? m_devicePanel->attenuationDb() : 0;
        const double rfGainDb = m_devicePanel ? m_devicePanel->rfGainDb() : 0.0;
        const double compensated = dbm + attenDb - rfGainDb;
        m_vfoPanel->setSignalDbm(compensated);
        emit meterDbmChanged(compensated);
    });

    QMetaObject::invokeMethod(
        m_rxAudio, [this, inputSampleRate]() { m_rxAudio->open(m_channelIndex, inputSampleRate); },
        Qt::QueuedConnection);
    emit statusMessage(tr("Opening audio engine..."));

    // Same reasoning as the original MainWindow::connectToDevice() had for
    // its single receiver: context m_rxAudio (worker-thread-affine) makes
    // this a same-thread direct connection since connection is also on
    // workerThread, so WDSP demod work happens right there; context
    // m_spectrum is on this panel's own dedicated m_spectrumThread, so
    // that's a genuinely separate, auto-queued delivery running
    // concurrently with audio demod.
    if (useSecondDdc) {
        connect(connection, &RadioConnection::iqSamplesReady2, m_rxAudio, [this](const QVector<double> &iq) {
            for (int i = 0; i + 1 < iq.size(); i += 2) {
                m_rxAudio->feedSample(iq[i], iq[i + 1]);
            }
        });
        connect(connection, &RadioConnection::iqSamplesReady2, m_spectrum, [this](const QVector<double> &iq) {
            for (int i = 0; i + 1 < iq.size(); i += 2) {
                m_spectrum->feedSample(iq[i], iq[i + 1]);
            }
        });
    } else {
        connect(connection, &RadioConnection::iqSamplesReady, m_rxAudio, [this](const QVector<double> &iq) {
            for (int i = 0; i + 1 < iq.size(); i += 2) {
                m_rxAudio->feedSample(iq[i], iq[i + 1]);
            }
        });
        connect(connection, &RadioConnection::iqSamplesReady, m_spectrum, [this](const QVector<double> &iq) {
            for (int i = 0; i + 1 < iq.size(); i += 2) {
                m_spectrum->feedSample(iq[i], iq[i + 1]);
            }
        });
    }
}

void ReceiverPanel::stopAudio() {
    m_workerThread = nullptr;
    m_connection = nullptr;
    m_useSecondDdc = false;
    if (!m_rxAudio) {
        return;
    }
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

void ReceiverPanel::setConnected(bool connected) {
    m_vfoPanel->setConnected(connected);
    m_toolbar->setConnected(connected);
    if (!connected) {
        m_vfoPanel->setSignalDbm(-140.0);
        m_panadapter->setPassband(0.0, 0.0);
    } else {
        const FilterEntry f = m_vfoPanel->currentFilter();
        m_panadapter->setPassband(f.low, f.high);
    }
}

double ReceiverPanel::frequencyHz() const { return m_vfoPanel->frequencyHz(); }

void ReceiverPanel::setFrequencyHz(double hz) {
    m_vfoPanel->setFrequencyHz(hz);
    m_panadapter->setCenterFrequencyHz(hz);
    m_toolbar->setFrequencyHz(hz);
    m_vfoPanel->setBandLabel(m_toolbar->currentBandLabel());
}

void ReceiverPanel::retuneTo(double hz) {
    setFrequencyHz(hz);
    updateBandStack();
    emit frequencyChanged(hz);
}

void ReceiverPanel::updateBandStack() {
    const int bandIndex = m_toolbar->bandIndexForFrequency(m_vfoPanel->frequencyHz());
    if (bandIndex < 0) {
        return;
    }
    m_bandStack[bandIndex] = {m_vfoPanel->frequencyHz(), int(m_vfoPanel->rxMode()), m_vfoPanel->currentFilterIndex()};
}

void ReceiverPanel::repaintDisplays() {
    if (!m_spectrumDirty) {
        return;
    }
    m_spectrumDirty = false;

    // Zoom: show only the center slice of the full received span, stretched
    // across the same widget width - see ToolbarWidget::zoomFactor().
    const int zoom = qBound(1, m_toolbar->zoomFactor(), m_latestSpectrum.isEmpty() ? 1 : m_latestSpectrum.size());
    QVector<float> displaySpectrum = m_latestSpectrum;
    const double baseSampleRateHz = double(m_toolbar->sampleRateHz());
    double displaySampleRateHz = baseSampleRateHz;
    if (zoom > 1 && !displaySpectrum.isEmpty()) {
        const int n = displaySpectrum.size();
        const int cropped = std::max(1, n / zoom);
        const int start = (n - cropped) / 2;
        displaySpectrum = displaySpectrum.mid(start, cropped);
        displaySampleRateHz = baseSampleRateHz / zoom;
    }

    // Auto-scale the displayed dB range to the actual incoming data - see
    // the original MainWindow::repaintDisplays()'s comment (now per-panel,
    // same reasoning).
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

void ReceiverPanel::saveSettings(QSettings &settings) const {
    settings.setValue(QStringLiteral("frequencyHz"), m_vfoPanel->frequencyHz());
    settings.setValue(QStringLiteral("mode"), int(m_vfoPanel->rxMode()));
    settings.setValue(QStringLiteral("filterIndex"), m_vfoPanel->currentFilterIndex());
    settings.setValue(QStringLiteral("stepIndex"), m_vfoPanel->currentStepIndex());
    settings.setValue(QStringLiteral("afGainDb"), m_toolbar->afGainDb());
    settings.setValue(QStringLiteral("sampleRateHz"), m_toolbar->sampleRateHz());
    settings.setValue(QStringLiteral("zoomFactor"), m_toolbar->zoomFactor());
    settings.setValue(QStringLiteral("fps"), m_toolbar->fps());
    settings.setValue(QStringLiteral("agcMode"), int(m_toolbar->agcMode()));
    settings.setValue(QStringLiteral("agcTopDb"), m_toolbar->agcTopDb());
    settings.setValue(QStringLiteral("noiseBlankerMode"), int(m_toolbar->noiseBlankerMode()));
    settings.setValue(QStringLiteral("noiseReductionMode"), int(m_toolbar->noiseReductionMode()));
    settings.setValue(QStringLiteral("autoNotchEnabled"), m_toolbar->autoNotchEnabled());
    settings.setValue(QStringLiteral("spectralNoiseBlankerEnabled"), m_toolbar->spectralNoiseBlankerEnabled());
    settings.setValue(QStringLiteral("nr4SmoothingFactor"), m_toolbar->nr4SmoothingFactor());

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

void ReceiverPanel::loadSettings(QSettings &settings) {
    if (!settings.contains(QStringLiteral("frequencyHz"))) {
        return; // First run - nothing saved yet, keep the built-in defaults.
    }
    setFrequencyHz(settings.value(QStringLiteral("frequencyHz")).toDouble());
    // Mode first: setRxMode() repopulates the filter combo to that mode's
    // default list/selection, which the saved filterIndex then overrides.
    m_vfoPanel->setRxMode(RxMode(settings.value(QStringLiteral("mode")).toInt()));
    m_vfoPanel->setFilterIndex(settings.value(QStringLiteral("filterIndex")).toInt());
    m_vfoPanel->setStepIndex(settings.value(QStringLiteral("stepIndex")).toInt());
    m_toolbar->setAfGainDb(settings.value(QStringLiteral("afGainDb")).toDouble());
    m_toolbar->setSampleRateHz(settings.value(QStringLiteral("sampleRateHz"), 48000).toInt());
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
