#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>

struct DiscoveredDevice;
class RadioConnection;
class ReceiverPanel;
class WidebandPanel;
class SettingsDialog;
class AnalogMeterWidget;
class QAction;
class QAudioSink;
class QDockWidget;
class QIODevice;
class QThread;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void showDiscoveryDialog();
    void disconnectFromRadio();
    // Creates m_settingsDialog on first call, shows/raises it on later
    // ones - non-modal and kept alive for the app's whole lifetime rather
    // than recreated each time, so it remembers its own position/tab.
    void showSettingsDialog();

private:
    // Tears down any existing connection and sets up a fresh one to
    // device - the actual connect logic, factored out of
    // showDiscoveryDialog() so it doesn't require going through the
    // modal dialog (e.g. for testing against hpsdrsim without a GUI click).
    void connectToDevice(const DiscoveredDevice &device);

    // Runs on m_workerThread (writes to QAudioSink, which can block). Mixes
    // RX1/RX2's own audioBlockReady blocks (simple additive mix, clamped)
    // before the single shared QAudioSink write - see m_rx1Block/m_rx2Block.
    void mixAndPlayAudio();
    // Persists/restores each receiver's own settings (ReceiverPanel::save/
    // loadSettings()), the shared DevicePanel's settings, whether RX2 is
    // enabled, and the dock layout (QMainWindow::saveState()/restoreState()).
    // loadSettings() runs once at startup before any device connection;
    // saveSettings() runs from the destructor.
    void saveSettings();
    void loadSettings();

    // m_connection (OldProtocolConnection or NewProtocolConnection,
    // depending on the connected device's protocol - see connectToDevice())
    // lives on m_workerThread, along with both receivers' RxAudioChannel
    // (WDSP audio demod) - see ReceiverPanel::startAudio(). Each
    // ReceiverPanel's SpectrumAnalyzer (panadapter FFT) lives on its own
    // dedicated thread instead (see ReceiverPanel's class comment) - both
    // are independent consumers of the same raw I/Q stream, so they get
    // separate cores rather than sharing one.
    QThread *m_workerThread = nullptr;
    RadioConnection *m_connection = nullptr;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioDevice = nullptr;
    QAction *m_disconnectAction = nullptr;
    QAction *m_rx2EnabledAction = nullptr;

    ReceiverPanel *m_rx1 = nullptr;
    ReceiverPanel *m_rx2 = nullptr;
    WidebandPanel *m_wideband = nullptr;
    SettingsDialog *m_settingsDialog = nullptr;

    // Analog S-meter - deliberately its OWN floating dock per receiver,
    // not embedded in VfoPanel (an earlier attempt at that rendered badly
    // in VfoPanel's cramped, wide-but-short meter row - see
    // AnalogMeterWidget's own comment on the geometry bug that caused).
    // Hidden by default, shown/hidden together via SettingsDialog's Meter
    // tab toggle.
    AnalogMeterWidget *m_rx1MeterWidget = nullptr;
    AnalogMeterWidget *m_rx2MeterWidget = nullptr;
    QDockWidget *m_rx1MeterDock = nullptr;
    QDockWidget *m_rx2MeterDock = nullptr;

    // Latest block from each receiver, mixed together in mixAndPlayAudio()
    // whenever both are present - RX2 stays silent (block never updated)
    // while disabled/not open.
    QVector<float> m_rx1Block;
    QVector<float> m_rx2Block;
};

#endif // MAINWINDOW_H
