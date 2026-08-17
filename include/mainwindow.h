#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class OldProtocolConnection;
class RxAudioChannel;
class SpectrumAnalyzer;
class VfoPanel;
class PanadapterWidget;
class WaterfallWidget;
class QAction;
class QAudioSink;
class QIODevice;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void showDiscoveryDialog();
    void disconnectFromRadio();

private:
    void playAudioBlock(const QVector<float> &interleavedStereo);

    OldProtocolConnection *m_connection = nullptr;
    RxAudioChannel *m_rxAudio = nullptr;
    SpectrumAnalyzer *m_spectrum = nullptr;
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioDevice = nullptr;
    QAction *m_disconnectAction = nullptr;
    VfoPanel *m_vfoPanel = nullptr;
    PanadapterWidget *m_panadapter = nullptr;
    WaterfallWidget *m_waterfall = nullptr;
};

#endif // MAINWINDOW_H
