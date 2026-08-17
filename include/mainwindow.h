#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>

class OldProtocolConnection;
class RxAudioChannel;
class SpectrumAnalyzer;
class VfoPanel;
class PanadapterWidget;
class WaterfallWidget;
class QAction;
class QAudioSink;
class QIODevice;
class QThread;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void showDiscoveryDialog();
    void disconnectFromRadio();

private:
    // Runs on m_workerThread (writes to QAudioSink, which can block).
    void playAudioBlock(const QVector<float> &interleavedStereo);
    void repaintDisplays();

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
};

#endif // MAINWINDOW_H
