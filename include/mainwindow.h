#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class OldProtocolConnection;
class RxAudioChannel;
class VfoPanel;
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
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioDevice = nullptr;
    QAction *m_disconnectAction = nullptr;
    VfoPanel *m_vfoPanel = nullptr;
};

#endif // MAINWINDOW_H
