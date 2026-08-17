#ifndef QHPSDR_DISCOVERYDIALOG_H
#define QHPSDR_DISCOVERYDIALOG_H

#include <QDialog>
#include <optional>

#include "discovered.h"

class DiscoveryService;
class QTableWidget;
class QPushButton;
class QLineEdit;
class QLabel;

// Qt replacement for deskHPSDR's GTK discovery dialog (core/deskhpsdr-src/discovery.c).
// Lists devices found by DiscoveryService and lets the user pick one.
class DiscoveryDialog : public QDialog {
    Q_OBJECT

public:
    explicit DiscoveryDialog(QWidget *parent = nullptr);

    // Valid only after the dialog was accepted.
    std::optional<DiscoveredDevice> selectedDevice() const { return m_selected; }

private slots:
    void startDiscovery();
    void onDeviceFound(const DiscoveredDevice &device);
    void onDiscoveryFinished();
    void onRowActivated(int row);

private:
    void addRow(const DiscoveredDevice &device);

    DiscoveryService *m_service = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_discoverButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QList<DiscoveredDevice> m_rowDevices;
    std::optional<DiscoveredDevice> m_selected;
};

#endif // QHPSDR_DISCOVERYDIALOG_H
