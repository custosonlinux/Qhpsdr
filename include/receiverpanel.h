#ifndef QHPSDR_RECEIVERPANEL_H
#define QHPSDR_RECEIVERPANEL_H

#include <QMap>
#include <QWidget>

class VfoPanel;
class PanadapterWidget;
class WaterfallWidget;
class ToolbarWidget;
class DevicePanel;
class RxAudioChannel;
class SpectrumAnalyzer;
class RadioConnection;
class QThread;
class QTimer;

// A single-entry-per-band "band stack" - see deskHPSDR's band_menu.c/
// bandstack_menu.c (several stored frequency/mode slots per band, cycled
// by repeatedly clicking the same band); simplified here to just the
// last-used one. mode/filterIndex stored as plain int (not RxMode) to
// avoid pulling rxaudio.h into this header for what's otherwise
// ReceiverPanel-private state.
struct BandStackEntry {
    double frequencyHz = 0.0;
    int mode = 0;
    int filterIndex = 0;
};

// One full receive chain end to end: VFO bar + panadapter/waterfall + its
// own per-receiver toolbar (AGC/NB/NR/AF-gain/band/zoom/FPS...) + its own
// WDSP RxAudioChannel + its own SpectrumAnalyzer (FFT for the display) +
// its own band-stack. Two of these exist side by side in MainWindow (RX1 =
// WDSP channel 0, RX2 = channel 1) - see RxAudioChannel::open()'s doc
// comment for why a second, differently-numbered channel is all WDSP
// itself needs to run two fully independent demodulators concurrently.
//
// RF Gain/Attenuation/Filter-Board/Wideband are deliberately NOT here -
// those are shared-hardware-level settings (one physical ADC feeds both
// receivers) that live on DevicePanel instead; see setDevicePanel().
// Likewise, this class never touches RadioConnection or QAudioSink
// directly (both are shared, owned by MainWindow) - user-driven retuning
// and demodulated audio cross that boundary via frequencyChanged()/
// audioBlockReady(), which MainWindow wires to the shared objects.
class ReceiverPanel : public QWidget {
    Q_OBJECT

public:
    // channelIndex is the WDSP RXA channel number (0 or 1 - must be unique
    // among concurrently-open RxAudioChannels, see rxaudio.h's doc
    // comment). label is used only for the initial window/dock title
    // MainWindow gives this panel ("RX1"/"RX2").
    ReceiverPanel(int channelIndex, const QString &label, QWidget *parent = nullptr);
    ~ReceiverPanel() override;

    // Non-owning - MainWindow owns the single shared DevicePanel (hosted
    // in WidebandPanel) and calls this once right after constructing both,
    // so meterUpdated's dBm compensation has somewhere to read current
    // RF-gain/attenuation values from on demand, without a dedicated
    // change-signal wired through for every one of DevicePanel's fields.
    void setDevicePanel(DevicePanel *devicePanel);

    // Creates and opens this receiver's RxAudioChannel on workerThread
    // (the thread shared by both receivers' WDSP work - see MainWindow's
    // class comment) at WDSP channel channelIndex, and wires connection's
    // per-DDC I/Q stream to it and to this panel's own SpectrumAnalyzer -
    // RadioConnection::iqSamplesReady2 if useSecondDdc, else
    // ::iqSamplesReady. Safe to call again after stopAudio() for a fresh
    // connection.
    void startAudio(QThread *workerThread, int inputSampleRate, RadioConnection *connection, bool useSecondDdc);
    // Closes and destroys this receiver's RxAudioChannel - call before the
    // shared RadioConnection disconnects/is destroyed.
    void stopAudio();

    void setConnected(bool connected);

    double frequencyHz() const;
    // Sets displayed/tuned frequency without emitting frequencyChanged() -
    // for syncing to state that changed elsewhere (e.g. loadSettings()).
    void setFrequencyHz(double hz);

    // Persists/restores this receiver's own frequency, mode, filter, step,
    // AF gain, zoom, AGC/noise-blanker settings and band stack - assumes
    // the caller has already called settings.beginGroup()/endGroup()
    // around this (see MainWindow::saveSettings()/loadSettings()).
    void saveSettings(class QSettings &settings) const;
    void loadSettings(class QSettings &settings);

    VfoPanel *vfoPanel() const { return m_vfoPanel; }
    ToolbarWidget *toolbar() const { return m_toolbar; }
    PanadapterWidget *panadapter() const { return m_panadapter; }

signals:
    // User (or band-stack recall) retuned this receiver - MainWindow
    // forwards this to the shared RadioConnection's setRxFrequency()
    // (RX1) or setRxFrequency2() (RX2).
    void frequencyChanged(double hz);
    // User changed this receiver's DDC sample rate - MainWindow forwards
    // this to the shared RadioConnection's setRxSampleRate()/2(). The
    // RxAudioChannel-side reopen at the new rate is handled internally
    // (see onSampleRateChanged() in the .cpp) - MainWindow doesn't need to
    // do anything beyond the RadioConnection push.
    void sampleRateChanged(int hz);
    // Interleaved stereo, ready to mix with the other receiver's block and
    // hand to the shared QAudioSink - forwarded from this panel's
    // RxAudioChannel::audioBlockReady().
    void audioBlockReady(QVector<float> interleavedStereo);
    // Forwarded from RxAudioChannel/this panel's own state so MainWindow
    // can show it in the status bar without reaching into internals.
    void statusMessage(QString message);
    // Same compensated dBm value just pushed to this panel's own digital
    // meter (VfoPanel::setSignalDbm()) - MainWindow feeds this to the
    // separate floating AnalogMeterWidget dock, see MainWindow's class
    // comment on why the analog meter isn't embedded in VfoPanel.
    void meterDbmChanged(double dbm);

private:
    void retuneTo(double hz);
    void updateBandStack();
    void repaintDisplays();

    int m_channelIndex = 0;
    RxAudioChannel *m_rxAudio = nullptr;
    SpectrumAnalyzer *m_spectrum = nullptr;
    QThread *m_spectrumThread = nullptr;
    DevicePanel *m_devicePanel = nullptr; // non-owning, see setDevicePanel()

    // Remembered from the last startAudio() call (cleared by stopAudio())
    // purely so a live sample-rate change can reopen RxAudioChannel with
    // the same context, without MainWindow having to redo the
    // audioBlockReady/iqSamplesReady wiring - see the .cpp's toolbar
    // sampleRateChanged handler. Non-owning, same as m_devicePanel.
    QThread *m_workerThread = nullptr;
    RadioConnection *m_connection = nullptr;
    bool m_useSecondDdc = false;

    VfoPanel *m_vfoPanel = nullptr;
    PanadapterWidget *m_panadapter = nullptr;
    WaterfallWidget *m_waterfall = nullptr;
    ToolbarWidget *m_toolbar = nullptr;

    // Panadapter/waterfall repainting is decoupled from the ~23/s rate
    // spectrum frames actually arrive at - see MainWindow's original
    // m_repaintTimer comment (now per-panel, same reasoning).
    QTimer *m_repaintTimer = nullptr;
    QVector<float> m_latestSpectrum;
    bool m_spectrumDirty = false;
    float m_dbFloor = -120.0f;
    float m_dbCeil = -40.0f;
    float m_waterfallFloor = -120.0f;
    float m_waterfallCeil = -90.0f;

    QMap<int, BandStackEntry> m_bandStack;
};

#endif // QHPSDR_RECEIVERPANEL_H
