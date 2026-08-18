#ifndef QHPSDR_OLDPROTOCOL_H
#define QHPSDR_OLDPROTOCOL_H

#include <QHostAddress>
#include <QObject>
#include <QVector>
#include <array>
#include <cstdint>

#include "discovered.h"
#include "radioconnection.h"

class QUdpSocket;
class QTimer;

// Qt-native replacement for deskHPSDR's old_protocol.c (HPSDR "Protocol 1" /
// METIS), scoped to what's needed to actually stream receive I/Q from a
// single receiver:
//
//  - METIS start/stop command (core/deskhpsdr-src/old_protocol.c:
//    metis_start_stop()).
//  - The 1032-byte METIS UDP frame: 8-byte header (EF FE 01 <endpoint>
//    <32-bit sequence>) followed by two 512-byte USB sub-frames
//    (metis_write()/ozy_send_buffer(), receive_thread()).
//  - Each USB sub-frame: 3-byte sync (0x7F 0x7F 0x7F), C0 "register
//    select" byte, C1-C4 register payload, then (on receive) 63
//    sample-sets of 3-byte I + 3-byte Q + 2-byte mic = 8 bytes each.
//
// NOT yet implemented (left for later steps): TX I/Q, local microphone
// audio, antenna/filter board registers, more than one receiver, TCP
// transport. Those are additional C&C registers in the same framing and
// can be added incrementally without changing the wire handling done here.
class OldProtocolConnection : public RadioConnection {
    Q_OBJECT

public:
    explicit OldProtocolConnection(QObject *parent = nullptr);

    void connectToDevice(const DiscoveredDevice &device, double rxFrequencyHz = 7100000.0) override;
    void disconnectFromDevice() override;
    bool isConnected() const override { return m_connected; }

    void setRxFrequency(double hz) override { m_rxFrequencyHz = hz; }
    double rxFrequency() const override { return m_rxFrequencyHz; }

    // Standard HPSDR step attenuator on ADC0 (core/deskhpsdr-src/
    // old_protocol.c: output_buffer[C4] = 0x20 | (adc[0].attenuation &
    // 0x1F), sent as the C0=0x14 register). 0-31 dB. Not applicable to
    // HermesLite2's different gain scheme, which isn't implemented here.
    void setAttenuation(int db) override { m_attenuationDb = qBound(0, db, 31); }
    int attenuation() const override { return m_attenuationDb; }

private slots:
    void readPendingDatagrams();
    void reportStats();

private:
    void sendStartStop(bool start);
    void sendOutputSubframe();
    void primeOutput();
    void parseIncomingPacket(const QByteArray &data);
    void parseSubframe(const uchar *frame);

    QUdpSocket *m_socket = nullptr;
    QTimer *m_statsTimer = nullptr;

    QHostAddress m_deviceAddress;
    quint16 m_devicePort = 1024;
    bool m_connected = false;

    quint32 m_sendSequence = 0;
    std::array<uchar, 1032> m_outputBuffer{};
    int m_outputOffset = 8; // 8 or 520: which half of m_outputBuffer we're filling
    int m_outputCommandState = 1; // 1 = TX/DUC freq, 2 = RX1 freq, 3 = ADC0 attenuator, next
    double m_rxFrequencyHz = 7100000.0;
    int m_attenuationDb = 0;

    quint64 m_packetsReceived = 0;
    quint64 m_samplesReceived = 0;
    quint64 m_lastStatsPackets = 0;
    quint64 m_lastStatsSamples = 0;
};

#endif // QHPSDR_OLDPROTOCOL_H
