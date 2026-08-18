#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QVector>

struct DiscoveredDevice;
class OldProtocolConnection;
class RxAudioChannel;
class SpectrumAnalyzer;
class VfoPanel;
class PanadapterWidget;
class WaterfallWidget;
class ToolbarWidget;
class QAction;
class QAudioSink;
class QIODevice;
class QThread;
class QTimer;

// A single-entry-per-band "band stack" (deskHPSDR's band_menu.c/
// bandstack_menu.c has several stored frequency/mode slots per band,
// cycled by repeatedly clicking the same band - simplified here to just
// the last-used one). mode/filterIndex stored as plain int (not RxMode)
// to avoid pulling rxaudio.h into this header for a forward-declared
// class's private state.
struct BandStackEntry {
    double frequencyHz = 0.0;
    int mode = 0;
    int filterIndex = 0;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void showDiscoveryDialog();
    void disconnectFromRadio();

private:
    // Tears down any existing connection and sets up a fresh one to
    // device - the actual connect logic, factored out of
    // showDiscoveryDialog() so it doesn't require going through the
    // modal dialog (e.g. for testing against hpsdrsim without a GUI click).
    void connectToDevice(const DiscoveredDevice &device);

    // Runs on m_workerThread (writes to QAudioSink, which can block).
    void playAudioBlock(const QVector<float> &interleavedStereo);
    void repaintDisplays();
    // Shared by every way the tuned frequency can change (typed, wheel,
    // panadapter click, band button): updates the VFO display, pushes the
    // new frequency to the radio, and keeps the panadapter and toolbar's
    // band combo in sync.
    void retuneTo(double hz);
    // Persists/restores frequency, mode, filter, step, attenuation, AF/RF
    // gain, zoom, AGC/noise-blanker and the band stack across restarts
    // (QSettings, org/app "Qhpsdr" - see main.cpp). loadSettings() runs
    // once at startup before any device connection; saveSettings() runs
    // from the destructor.
    void saveSettings();
    void loadSettings();
    // Captures the current frequency/mode/filter into m_bandStack, keyed
    // by whichever band (ToolbarWidget::bandIndexForFrequency()) the
    // current frequency falls in - a no-op outside all listed bands.
    // Called after every frequency/mode/filter change (matches
    // deskHPSDR's vfo_save_bandstack(): implicit, continuous, not tied to
    // an explicit save action) so switching back to a band later recalls
    // exactly where it was left, not just the band's center frequency.
    void updateBandStack();

    // OldProtocolConnection + RxAudioChannel (network I/O and WDSP audio
    // demod) live on m_workerThread; SpectrumAnalyzer (the panadapter's
    // FFT) lives on its own m_spectrumThread. They're independent - both
    // just consume the same raw I/Q stream, neither depends on the
    // other's output - so they get separate threads/cores rather than
    // sharing one, instead of leaving cores idle. Only thin signal
    // emissions cross back to the GUI thread for actual widget updates.
    QThread *m_workerThread = nullptr;
    QThread *m_spectrumThread = nullptr;
    OldProtocolConnection *m_connection = nullptr;
    RxAudioChannel *m_rxAudio = nullptr;
    SpectrumAnalyzer *m_spectrum = nullptr;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioDevice = nullptr;
    QAction *m_disconnectAction = nullptr;
    VfoPanel *m_vfoPanel = nullptr;
    PanadapterWidget *m_panadapter = nullptr;
    WaterfallWidget *m_waterfall = nullptr;
    ToolbarWidget *m_toolbar = nullptr;

    // Panadapter/waterfall repainting is decoupled from the ~23/s rate
    // spectrum frames actually arrive at: painting synchronously on every
    // frame made each incoming frame's handler expensive enough (grid +
    // trace + image-scroll + widget repaint) to noticeably starve the GUI
    // event loop. m_repaintTimer instead pulls whatever's latest in
    // m_latestSpectrum at a fixed, bounded rate.
    QTimer *m_repaintTimer = nullptr;
    QVector<float> m_latestSpectrum;
    bool m_spectrumDirty = false;
    float m_dbFloor = -120.0f;
    float m_dbCeil = -40.0f;

    // Keyed by ToolbarWidget::bandIndexForFrequency() - see
    // updateBandStack(). Persisted via save/loadSettings().
    QMap<int, BandStackEntry> m_bandStack;
};

#endif // MAINWINDOW_H
