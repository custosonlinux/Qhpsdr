#include "oldprotocol.h"

#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>

namespace {
constexpr int kUsbFrameSize = 512;
constexpr int kMetisFrameSize = 1032;
constexpr uchar kSync = 0x7F;

constexpr int SYNC0 = 0, SYNC1 = 1, SYNC2 = 2, C0 = 3, C1 = 4, C2 = 5, C3 = 6, C4 = 7;

constexpr uchar SPEED_48K = 0x00;
constexpr uchar CONFIG_MERCURY = 0x40;

void put32BE(uchar *dst, quint32 value) {
    dst[0] = uchar(value >> 24);
    dst[1] = uchar(value >> 16);
    dst[2] = uchar(value >> 8);
    dst[3] = uchar(value);
}

qint32 get24BESigned(const uchar *p) {
    qint32 v = (qint32(p[0]) << 16) | (qint32(p[1]) << 8) | qint32(p[2]);
    if (v & 0x00800000) {
        v |= ~0x00FFFFFF; // sign-extend 24 -> 32 bits
    }
    return v;
}

} // namespace

OldProtocolConnection::OldProtocolConnection(QObject *parent) : QObject(parent) {
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &OldProtocolConnection::readPendingDatagrams);

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, &OldProtocolConnection::reportStats);
}

void OldProtocolConnection::connectToDevice(const DiscoveredDevice &device, double rxFrequencyHz) {
    if (m_connected) {
        disconnectFromDevice();
    }

    m_deviceAddress = device.address;
    m_devicePort = device.port != 0 ? device.port : 1024;
    m_rxFrequencyHz = rxFrequencyHz;
    m_sendSequence = 0;
    m_outputOffset = 8;
    m_outputCommandState = 1;
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

    // Mirrors old_protocol_init()'s "prime radio" step: push a few silent
    // C&C frames before the start command so the radio's DUC FIFO doesn't
    // underrun/overrun the instant streaming begins.
    primeOutput();
    sendStartStop(true);

    m_connected = true;
    m_statsTimer->start();
    emit connected();
}

void OldProtocolConnection::disconnectFromDevice() {
    if (!m_connected) {
        return;
    }
    sendStartStop(false);
    m_statsTimer->stop();
    m_socket->close();
    m_connected = false;
    emit disconnected();
}

void OldProtocolConnection::sendStartStop(bool start) {
    std::array<uchar, 64> buf{};
    buf[0] = 0xEF;
    buf[1] = 0xFE;
    buf[2] = 0x04;
    buf[3] = start ? 0x01 : 0x00;
    m_socket->writeDatagram(reinterpret_cast<const char *>(buf.data()), qint64(buf.size()), m_deviceAddress,
                             m_devicePort);
}

void OldProtocolConnection::primeOutput() {
    // 504 silent audio/IQ samples = 8 USB sub-frames = 4 METIS packets,
    // same amount as old_protocol_init()'s priming loop.
    for (int i = 0; i < 8; ++i) {
        sendOutputSubframe();
    }
}

void OldProtocolConnection::sendOutputSubframe() {
    uchar frame[kUsbFrameSize] = {0};
    frame[SYNC0] = kSync;
    frame[SYNC1] = kSync;
    frame[SYNC2] = kSync;

    // Every other sub-frame is the mandatory "C0=0" info packet (sample
    // rate / board config); the alternating ones cycle through register
    // writes. We only implement the two registers needed to get RX1
    // streaming: TX/DUC frequency (unused while not transmitting, but the
    // radio expects a value) and the RX1 DDC frequency.
    if (m_outputOffset == 8) {
        frame[C0] = 0x00;
        frame[C1] = SPEED_48K | CONFIG_MERCURY;
        frame[C2] = 0x00;
        frame[C3] = 0x00;
        frame[C4] = 0x00;
    } else if (m_outputCommandState == 1) {
        frame[C0] = 0x02; // TX/DUC frequency
        put32BE(&frame[C1], quint32(m_rxFrequencyHz));
        m_outputCommandState = 2;
    } else {
        frame[C0] = 0x04; // RX1 DDC frequency
        put32BE(&frame[C1], quint32(m_rxFrequencyHz));
        m_outputCommandState = 1;
    }
    // Payload (mic audio / TX I-Q, offset 8..511): silence, since local
    // mic input and TX I/Q aren't wired up yet.

    std::copy(std::begin(frame), std::end(frame), m_outputBuffer.begin() + m_outputOffset);

    if (m_outputOffset == 8) {
        m_outputOffset = 520;
        return;
    }

    m_outputBuffer[0] = 0xEF;
    m_outputBuffer[1] = 0xFE;
    m_outputBuffer[2] = 0x01;
    m_outputBuffer[3] = 0x02; // endpoint 2: C&C + mic/TX-IQ out
    put32BE(&m_outputBuffer[4], m_sendSequence++);

    m_socket->writeDatagram(reinterpret_cast<const char *>(m_outputBuffer.data()), qint64(m_outputBuffer.size()),
                             m_deviceAddress, m_devicePort);
    m_outputOffset = 8;
}

void OldProtocolConnection::readPendingDatagrams() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray buffer;
        buffer.resize(int(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        m_socket->readDatagram(buffer.data(), buffer.size(), &sender);
        if (sender != m_deviceAddress) {
            continue;
        }
        parseIncomingPacket(buffer);

        // Pace our output 1:1 with the radio's data stream, same as
        // deskHPSDR's receive-thread-driven flow control - avoids relying
        // on a separately-timed QTimer to hit the right cadence.
        sendOutputSubframe();
    }
}

void OldProtocolConnection::parseIncomingPacket(const QByteArray &data) {
    if (data.size() != kMetisFrameSize) {
        return;
    }
    const auto *bytes = reinterpret_cast<const uchar *>(data.constData());
    if (bytes[0] != 0xEF || bytes[1] != 0xFE || bytes[2] != 0x01) {
        return;
    }
    ++m_packetsReceived;
    parseSubframe(bytes + 8);
    parseSubframe(bytes + 520);
}

void OldProtocolConnection::parseSubframe(const uchar *frame) {
    if (frame[SYNC0] != kSync || frame[SYNC1] != kSync || frame[SYNC2] != kSync) {
        return;
    }
    // frame[C0] carries PTT/dot/dash status bits on receive; not consumed
    // yet since TX isn't implemented.

    // Payload: 63 sample-sets of 2-byte mic + 3-byte I + 3-byte Q (single
    // receiver, the default until multi-DDC support is added).
    constexpr double kScale = 1.0 / 8388608.0; // 2^23, 24-bit signed ADC sample
    QVector<double> iq;
    iq.reserve(63 * 2);
    const uchar *p = frame + 8;
    for (int i = 0; i < 63; ++i, p += 8) {
        // p[0..1]: mic sample (unused for now)
        qint32 iSample = get24BESigned(p + 2);
        qint32 qSample = get24BESigned(p + 5);
        iq.append(double(iSample) * kScale);
        iq.append(double(qSample) * kScale);
        ++m_samplesReceived;
    }
    emit iqSamplesReady(iq);
}

void OldProtocolConnection::reportStats() {
    const quint64 packets = m_packetsReceived - m_lastStatsPackets;
    const quint64 samples = m_samplesReceived - m_lastStatsSamples;
    m_lastStatsPackets = m_packetsReceived;
    m_lastStatsSamples = m_samplesReceived;
    emit statsUpdated(m_packetsReceived, m_samplesReceived, double(samples));
    Q_UNUSED(packets);
}
