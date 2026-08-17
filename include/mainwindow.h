#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class OldProtocolConnection;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void showDiscoveryDialog();
    void disconnectFromRadio();

private:
    OldProtocolConnection *m_connection = nullptr;
    QAction *m_disconnectAction = nullptr;
};

#endif // MAINWINDOW_H
