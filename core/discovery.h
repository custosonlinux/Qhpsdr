#ifndef QHPSDR_DISCOVERY_H
#define QHPSDR_DISCOVERY_H

#include <QHostAddress>
#include <QList>
#include <QObject>

#include "discovered.h"

class QUdpSocket;
class QTimer;

// Qt-native replacement for deskHPSDR's old_discovery.c / new_discovery.c.
//
// Sends the same HPSDR protocol 1 ("METIS", 63-byte) and protocol 2
// (60-byte) discovery probes as broadcast on every active IPv4 interface
// plus, optionally, as a unicast probe to a specific host. Responses are
// collected asynchronously via a single QUdpSocket instead of the original
// blocking-recv-in-a-GThread approach - no separate thread is needed.
class DiscoveryService : public QObject {
    Q_OBJECT

public:
    explicit DiscoveryService(QObject *parent = nullptr);

    // Starts a discovery run. If targetHost is non-empty, only that host is
    // probed (both protocols); otherwise every active, non-loopback IPv4
    // interface is broadcast on. Emits deviceFound() as responses arrive and
    // finished() once the collection window closes.
    void start(const QString &targetHost = QString(), quint16 port = 1024);
    void stop();

    const QList<DiscoveredDevice> &devices() const { return m_devices; }

signals:
    void deviceFound(const DiscoveredDevice &device);
    void finished();

private slots:
    void readPendingDatagrams();
    void onCollectionTimeout();

private:
    void sendBroadcastProbes();
    void sendTargetedProbes(const QHostAddress &target);
    void sendProbe(const QHostAddress &to);
    void handleDatagram(const QByteArray &data, const QHostAddress &sender);
    void parseOldProtocolResponse(const QByteArray &data, const QHostAddress &sender);
    void parseNewProtocolResponse(const QByteArray &data, const QHostAddress &sender);
    bool deviceAlreadyKnown(const QHostAddress &sender, int protocol) const;

    QUdpSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    QList<DiscoveredDevice> m_devices;
    quint16 m_port = 1024;
};

#endif // QHPSDR_DISCOVERY_H
