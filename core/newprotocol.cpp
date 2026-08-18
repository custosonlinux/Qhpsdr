#include "newprotocol.h"

#include <QTimer>
#include <QUdpSocket>
#include <array>

namespace {

// Protocol 2 host<->radio ports (core/deskhpsdr-src/new_protocol.h). Only
// the ones this receive-only subset actually uses.
constexpr quint16 kGeneralPort = 1024;          // general packet, host -> radio
constexpr quint16 kReceiveSpecificPort = 1025;  // receive-specific packet, host -> radio
constexpr quint16 kHighPriorityPort = 1027;     // high-priority packet, host -> radio
constexpr quint16 kRxIqBasePort = 1035;         // radio -> host, +ddc index (source port)
constexpr int kMaxDdc = 4;

constexpr int kGeneralPacketSize = 60;
constexpr int kReceiveSpecificPacketSize = 1444;
constexpr int kHighPriorityPacketSize = 1444;
constexpr int kRxIqPacketSize = 1444;
constexpr int kRxIqSamplesPerPacket = 238;

// freqHz -> 32-bit DDC/DUC tuning phase word: 2^32 / 122.88MHz (the
// standard HPSDR Protocol 2 reference clock), matching
// p2_write_ddc_frequency_word() in new_protocol.c bit for bit (including
// its truncating, non-rounding cast).
constexpr double kPhaseWordPerHz = 34.952533333333333333333333333333;

void putU32BE(uchar *dst, quint32 value) {
    dst[0] = uchar(value >> 24);
    dst[1] = uchar(value >> 16);
    dst[2] = uchar(value >> 8);
    dst[3] = uchar(value);
}

void putU16BE(uchar *dst, quint16 value) {
    dst[0] = uchar(value >> 8);
    dst[1] = uchar(value);
}

qint32 get24BESigned(const uchar *p) {
    qint32 v = (qint32(p[0]) << 16) | (qint32(p[1]) << 8) | qint32(p[2]);
    if (v & 0x00800000) {
        v |= ~0x00FFFFFF; // sign-extend 24 -> 32 bits
    }
    return v;
}

} // namespace

NewProtocolConnection::NewProtocolConnection(QObject *parent) : RadioConnection(parent) {
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &NewProtocolConnection::readPendingDatagrams);

    // Resends the receive-specific and high-priority packets on a fixed
    // schedule rather than only once at connect - both need to stay fresh
    // for retuning (frequency lives in the high-priority packet) and, per
    // newhpsdrsim.c's ddc_specific_thread()/highprio_thread(), the radio
    // (re)creates its own receiving threads on every 0->1 "run" transition,
    // so a receive-specific packet sent before that thread exists would
    // otherwise be silently lost - periodic resends make that self-healing
    // instead of a hard ordering requirement.
    m_periodicTimer = new QTimer(this);
    m_periodicTimer->setInterval(200);
    connect(m_periodicTimer, &QTimer::timeout, this, &NewProtocolConnection::sendPeriodicPackets);

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, &NewProtocolConnection::reportStats);
}

void NewProtocolConnection::connectToDevice(const DiscoveredDevice &device, double rxFrequencyHz) {
    if (m_connected) {
        disconnectFromDevice();
    }

    m_deviceAddress = device.address;
    m_rxFrequencyHz = rxFrequencyHz;
    m_generalSequence = 0;
    m_receiveSpecificSequence = 0;
    m_highPrioritySequence = 0;
    m_packetsReceived = 0;
    m_samplesReceived = 0;
    m_lastStatsPackets = 0;
    m_lastStatsSamples = 0;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
    if (!m_socket->bind(QHostAddress::AnyIPv4, 0)) {
        emit errorOccurred(tr("Failed to bind UDP socket: %1").arg(m_socket->errorString()));
        return;
    }

    // Order matters for the simulator (see the periodic-resend comment
    // above): the general packet first so the radio knows where to reply,
    // then high-priority (spawns its receive threads), then
    // receive-specific - the periodic timer keeps re-sending both after
    // this so a lost first receive-specific packet corrects itself well
    // within a second.
    sendGeneralPacket();
    sendHighPriorityPacket();
    sendReceiveSpecificPacket();

    m_connected = true;
    m_periodicTimer->start();
    m_statsTimer->start();
    emit connected();
}

void NewProtocolConnection::disconnectFromDevice() {
    if (!m_connected) {
        return;
    }
    // High-priority packet with the run bit cleared - matches
    // new_protocol_menu_stop() sending a final high_priority() with
    // P2running already 0, which is how the radio/simulator knows to stop
    // streaming and tear down its per-DDC threads.
    m_highPrioritySequence = 0;
    std::array<uchar, kHighPriorityPacketSize> buf{};
    putU32BE(buf.data(), m_highPrioritySequence);
    m_socket->writeDatagram(reinterpret_cast<const char *>(buf.data()), qint64(buf.size()), m_deviceAddress,
                             kHighPriorityPort);

    m_periodicTimer->stop();
    m_statsTimer->stop();
    m_socket->close();
    m_connected = false;
    emit disconnected();
}

void NewProtocolConnection::sendGeneralPacket() {
    std::array<uchar, kGeneralPacketSize> buf{};
    putU32BE(buf.data(), m_generalSequence++);
    // buf[4] = 0x00 (already zero) identifies this as the general packet.
    // All per-stream port-override fields (bytes 5-22) are left zero, so
    // the radio uses its documented defaults (new_protocol.h) - exactly
    // what deskHPSDR itself does.
    buf[37] = 0x08; // phase word (not frequency), matches new_protocol_general()
    buf[38] = 0x01; // enable hardware timer
    // buf[58]/[59] (PA/Alex filter board enable) stay 0 - no PA, no filter
    // board wired up yet.
    m_socket->writeDatagram(reinterpret_cast<const char *>(buf.data()), qint64(buf.size()), m_deviceAddress,
                             kGeneralPort);
}

void NewProtocolConnection::sendReceiveSpecificPacket() {
    std::array<uchar, kReceiveSpecificPacketSize> buf{};
    putU32BE(buf.data(), m_receiveSpecificSequence++);
    buf[4] = 1;    // number of ADCs
    buf[7] = 0x01; // DDC0 enable bit
    // DDC0: buf[17 + ddc*6] = adc index, buf[18..19 + ddc*6] = sample rate
    // in kHz (big-endian), buf[22 + ddc*6] = bits per sample.
    buf[17] = 0; // ADC0
    putU16BE(&buf[18], 48); // 48kHz - matches RxAudioChannel's fixed 48kHz input
    buf[22] = 24;
    m_socket->writeDatagram(reinterpret_cast<const char *>(buf.data()), qint64(buf.size()), m_deviceAddress,
                             kReceiveSpecificPort);
}

void NewProtocolConnection::sendHighPriorityPacket() {
    std::array<uchar, kHighPriorityPacketSize> buf{};
    putU32BE(buf.data(), m_highPrioritySequence++);
    buf[4] = 0x01; // run bit (bit 0) - MOX (bit 1) stays 0, no TX support yet
    const quint32 phase = quint32(double(m_rxFrequencyHz) * kPhaseWordPerHz);
    putU32BE(&buf[9], phase); // DDC0 frequency word
    m_socket->writeDatagram(reinterpret_cast<const char *>(buf.data()), qint64(buf.size()), m_deviceAddress,
                             kHighPriorityPort);
}

void NewProtocolConnection::sendPeriodicPackets() {
    sendHighPriorityPacket();
    sendReceiveSpecificPacket();
}

void NewProtocolConnection::readPendingDatagrams() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray buffer;
        buffer.resize(int(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_socket->readDatagram(buffer.data(), buffer.size(), &sender, &senderPort);
        if (sender != m_deviceAddress) {
            continue;
        }
        // Demux by the sender's source port, same as deskHPSDR's own
        // new_protocol_thread(): the radio uses a different source port
        // per stream (high-priority-to-host, mic, and one per DDC), all
        // sent to the same host address+port registered by the general
        // packet - see the class comment.
        if (senderPort >= kRxIqBasePort && senderPort < kRxIqBasePort + kMaxDdc) {
            parseRxIqPacket(reinterpret_cast<const uchar *>(buffer.constData()), buffer.size());
        }
        // Other stream types (high-priority-to-host status, mic/line audio)
        // aren't consumed yet.
    }
}

void NewProtocolConnection::parseRxIqPacket(const uchar *data, int length) {
    if (length != kRxIqPacketSize) {
        return;
    }
    // Header: 4-byte sequence, 8 unused bytes, 0, bits-per-sample byte
    // (always 24), 0, sample-count byte - see newhpsdrsim.c's rx_thread().
    const int sampleCount = qMin(int(data[15]), kRxIqSamplesPerPacket);
    ++m_packetsReceived;

    constexpr double kScale = 1.0 / 8388608.0; // 2^23, 24-bit signed sample
    QVector<double> iq;
    iq.reserve(sampleCount * 2);
    const uchar *p = data + 16;
    for (int i = 0; i < sampleCount; ++i, p += 6) {
        const qint32 iSample = get24BESigned(p);
        const qint32 qSample = get24BESigned(p + 3);
        iq.append(double(iSample) * kScale);
        iq.append(double(qSample) * kScale);
        ++m_samplesReceived;
    }
    emit iqSamplesReady(iq);
}

void NewProtocolConnection::reportStats() {
    const quint64 samples = m_samplesReceived - m_lastStatsSamples;
    m_lastStatsPackets = m_packetsReceived;
    m_lastStatsSamples = m_samplesReceived;
    emit statsUpdated(m_packetsReceived, m_samplesReceived, double(samples));
}
