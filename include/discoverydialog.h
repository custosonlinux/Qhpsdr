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
class QComboBox;

// A saved "Connect Directly" address (core/oldprotocol.h's target), so the
// user doesn't have to retype an IP every time - e.g. connecting to a home
// radio over VPN while away, where broadcast discovery doesn't work (see
// DiscoveryDialog::connectDirectly()). Persisted via QSettings, not tied to
// any particular discovery result.
struct DeviceFavorite {
    QString name;
    QString description;
    QString host;
    quint16 port = 1024;
    // ORIGINAL_PROTOCOL or NEW_PROTOCOL (discovered.h) - which connection
    // class to use, since a direct-by-IP connect has no discovery reply to
    // infer this from.
    int protocol = ORIGINAL_PROTOCOL;
};

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
    void connectDirectly();

private:
    void addRow(const DiscoveredDevice &device);
    void loadFavorites();
    void saveFavorites();
    void refreshFavoritesCombo();
    void manageFavorites();
    // Small modal name+description prompt, prefilled from initialName/
    // initialDescription. Returns false if the user cancelled or left the
    // name empty.
    bool promptFavoriteDetails(const QString &initialName, const QString &initialDescription, QString &nameOut,
                                QString &descriptionOut);

    DiscoveryService *m_service = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_discoverButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_connectDirectButton = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QComboBox *m_protocolCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QComboBox *m_favoritesCombo = nullptr;
    QPushButton *m_saveFavoriteButton = nullptr;
    QPushButton *m_manageFavoritesButton = nullptr;
    QList<DeviceFavorite> m_favorites;
    QList<DiscoveredDevice> m_rowDevices;
    std::optional<DiscoveredDevice> m_selected;
};

#endif // QHPSDR_DISCOVERYDIALOG_H
