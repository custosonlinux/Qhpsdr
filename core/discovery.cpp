#include "discovery.h"

#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>

namespace {

// deskHPSDR sends a 63-byte protocol-1 ("METIS") discovery probe:
// EF FE 02 00 00 ... 00
QByteArray oldProtocolProbe() {
    QByteArray buf(63, '\0');
    buf[0] = char(0xEF);
    buf[1] = char(0xFE);
    buf[2] = 0x02;
    return buf;
}

// ...and a 60-byte protocol-2 discovery probe: 00 00 00 00 02 00 ... 00
QByteArray newProtocolProbe() {
    QByteArray buf(60, '\0');
    buf[4] = 0x02;
    return buf;
}

QString oldProtocolDeviceName(int device, int &softwareVersion, double &freqMin, double &freqMax,
                               const QByteArray &data) {
    freqMin = 0.0;
    switch (device) {
    case DEVICE_METIS:
        freqMax = 61440000.0;
        return QStringLiteral("Metis");
    case DEVICE_HERMES:
        freqMax = 61440000.0;
        return QStringLiteral("Hermes");
    case DEVICE_GRIFFIN:
        freqMax = 61440000.0;
        return QStringLiteral("Griffin");
    case DEVICE_ANGELIA:
        freqMax = 61440000.0;
        return QStringLiteral("Angelia");
    case DEVICE_ORION:
        freqMax = 61440000.0;
        return QStringLiteral("Orion");
    case DEVICE_HERMES_LITE: {
        // HermesLite V2 boards report software_version >= 40 and carry a
        // minor version byte at offset 21 (major.minor, e.g. 73.2).
        const quint8 major = quint8(data[9]);
        const quint8 minor = quint8(data[21]);
        softwareVersion = 10 * major + minor;
        freqMax = 38400000.0;
        if (softwareVersion < 400) {
            return QStringLiteral("HermesLite V1");
        }
        return QStringLiteral("HermesLite V2");
    }
    case DEVICE_ORION2:
        freqMax = 61440000.0;
        return QStringLiteral("Orion2");
    case DEVICE_STEMLAB:
        freqMax = 61440000.0;
        return QStringLiteral("STEMlab");
    case DEVICE_STEMLAB_Z20:
        freqMax = 61440000.0;
        return QStringLiteral("STEMlab-Zync7020");
    default:
        freqMax = 61440000.0;
        return QStringLiteral("Unknown");
    }
}

QString newProtocolDeviceName(int device, int softwareVersion, double &freqMin, double &freqMax) {
    freqMin = 0.0;
    switch (device) {
    case NEW_DEVICE_ATLAS:
        freqMax = 61440000.0;
        return QStringLiteral("Atlas");
    case NEW_DEVICE_HERMES:
        freqMax = 61440000.0;
        return QStringLiteral("Hermes");
    case NEW_DEVICE_HERMES2:
        freqMax = 61440000.0;
        return QStringLiteral("Hermes2");
    case NEW_DEVICE_ANGELIA:
        freqMax = 61440000.0;
        return QStringLiteral("Angelia");
    case NEW_DEVICE_ORION:
        freqMax = 61440000.0;
        return QStringLiteral("Orion");
    case NEW_DEVICE_ORION2:
        freqMax = 61440000.0;
        return QStringLiteral("Orion2");
    case NEW_DEVICE_SATURN:
        freqMax = 61440000.0;
        return QStringLiteral("Saturn/G2");
    case NEW_DEVICE_HERMES_LITE:
        freqMax = 30720000.0;
        return softwareVersion < 40 ? QStringLiteral("Hermes Lite V1") : QStringLiteral("Hermes Lite V2");
    default:
        freqMax = 30720000.0;
        return QStringLiteral("Unknown");
    }
}

} // namespace

DiscoveryService::DiscoveryService(QObject *parent) : QObject(parent) {
    m_socket = new QUdpSocket(this);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_socket, &QUdpSocket::readyRead, this, &DiscoveryService::readPendingDatagrams);
    connect(m_timer, &QTimer::timeout, this, &DiscoveryService::onCollectionTimeout);
}

void DiscoveryService::start(const QString &targetHost, quint16 port) {
    m_devices.clear();
    m_port = port;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
    m_socket->bind(QHostAddress::AnyIPv4, 0);

    if (!targetHost.isEmpty()) {
        sendTargetedProbes(QHostAddress(targetHost));
    } else {
        sendBroadcastProbes();
    }

    // The original implementation waits 5s (protocol 1) / 2s (protocol 2)
    // per interface in a blocking thread; here every probe listens on the
    // same socket concurrently, so one shared window is enough.
    m_timer->start(3000);
}

void DiscoveryService::stop() {
    m_timer->stop();
    m_socket->close();
}

void DiscoveryService::sendBroadcastProbes() {
    const auto oldProbe = oldProtocolProbe();
    const auto newProbe = newProtocolProbe();

    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
            (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress bcast = entry.broadcast();
            if (bcast.isNull() || entry.ip().protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            m_socket->writeDatagram(oldProbe, bcast, m_port);
            m_socket->writeDatagram(newProbe, bcast, m_port);
        }
    }
}

void DiscoveryService::sendTargetedProbes(const QHostAddress &target) {
    if (target.isNull()) {
        return;
    }
    sendProbe(target);
}

void DiscoveryService::sendProbe(const QHostAddress &to) {
    m_socket->writeDatagram(oldProtocolProbe(), to, m_port);
    m_socket->writeDatagram(newProtocolProbe(), to, m_port);
}

void DiscoveryService::readPendingDatagrams() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray buffer;
        buffer.resize(int(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        m_socket->readDatagram(buffer.data(), buffer.size(), &sender);
        handleDatagram(buffer, sender);
    }
}

void DiscoveryService::handleDatagram(const QByteArray &data, const QHostAddress &sender) {
    if (data.size() >= 3 && quint8(data[0]) == 0xEF && quint8(data[1]) == 0xFE) {
        parseOldProtocolResponse(data, sender);
    } else if (data.size() >= 5 && data.size() != 1444 && quint8(data[0]) == 0 && quint8(data[1]) == 0 &&
               quint8(data[2]) == 0 && quint8(data[3]) == 0) {
        // A 1444-byte packet is protocol-2 stream data, not a discovery
        // reply (e.g. a radio left streaming by a previous client) - skip.
        parseNewProtocolResponse(data, sender);
    }
}

bool DiscoveryService::deviceAlreadyKnown(const QHostAddress &sender, int protocol) const {
    for (const auto &d : m_devices) {
        if (d.address == sender && d.protocol == protocol) {
            return true;
        }
    }
    return false;
}

void DiscoveryService::parseOldProtocolResponse(const QByteArray &data, const QHostAddress &sender) {
    if (data.size() < 22) {
        return;
    }
    const int status = quint8(data[2]);
    if (status != STATE_AVAILABLE && status != STATE_SENDING) {
        return;
    }
    if (deviceAlreadyKnown(sender, ORIGINAL_PROTOCOL)) {
        return;
    }

    DiscoveredDevice dev;
    dev.protocol = ORIGINAL_PROTOCOL;
    dev.device = quint8(data[10]);
    dev.softwareVersion = quint8(data[9]);
    dev.status = status;
    dev.address = sender;
    dev.port = m_port;
    for (int i = 0; i < 6; ++i) {
        dev.macAddress[size_t(i)] = quint8(data[3 + i]);
    }

    double freqMin = 0.0, freqMax = 0.0;
    dev.name = oldProtocolDeviceName(dev.device, dev.softwareVersion, freqMin, freqMax, data);
    if (dev.device == DEVICE_HERMES_LITE && dev.softwareVersion >= 400) {
        dev.device = DEVICE_HERMES_LITE2;
    }
    dev.frequencyMin = freqMin;
    dev.frequencyMax = freqMax;
    dev.supportedReceivers = 2;

    m_devices.append(dev);
    emit deviceFound(dev);
}

void DiscoveryService::parseNewProtocolResponse(const QByteArray &data, const QHostAddress &sender) {
    if (data.size() < 24) {
        return;
    }
    const int status = quint8(data[4]);
    if (status != STATE_AVAILABLE && status != STATE_SENDING) {
        return;
    }
    if (deviceAlreadyKnown(sender, NEW_PROTOCOL)) {
        return;
    }

    DiscoveredDevice dev;
    dev.protocol = NEW_PROTOCOL;
    dev.device = quint8(data[11]) + 1000;
    dev.softwareVersion = quint8(data[13]);
    dev.status = status;
    dev.address = sender;
    dev.port = m_port;
    for (int i = 0; i < 6; ++i) {
        dev.macAddress[size_t(i)] = quint8(data[5 + i]);
    }

    double freqMin = 0.0, freqMax = 0.0;
    dev.name = newProtocolDeviceName(dev.device, dev.softwareVersion, freqMin, freqMax);
    if (dev.device == NEW_DEVICE_HERMES_LITE && dev.softwareVersion >= 40) {
        dev.device = NEW_DEVICE_HERMES_LITE2;
    }
    dev.frequencyMin = freqMin;
    dev.frequencyMax = freqMax;
    dev.supportedReceivers = 2;

    m_devices.append(dev);
    emit deviceFound(dev);
}

void DiscoveryService::onCollectionTimeout() {
    m_socket->close();
    emit finished();
}
