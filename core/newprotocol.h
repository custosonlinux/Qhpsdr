#ifndef QHPSDR_NEWPROTOCOL_H
#define QHPSDR_NEWPROTOCOL_H

#include <QHostAddress>
#include <cstdint>

#include "radioconnection.h"

class QUdpSocket;
class QTimer;

// Qt-native implementation of a useful subset of HPSDR "Protocol 2"
// (core/deskhpsdr-src/new_protocol.c), scoped - like OldProtocolConnection -
// to receive-only, single-DDC operation. The wire format below was derived
// from new_protocol.c's packet-construction functions and cross-checked
// against core/deskhpsdr-src/newhpsdrsim.c (the simulator used to validate
// this class), since the two occasionally reveal details deskHPSDR's own
// host-side code leaves implicit (e.g. which UDP port replies actually
// arrive on: the radio just remembers whichever host address+port the
// General packet came from and sends every reply there, regardless of the
// per-stream port numbers new_protocol.h documents - see
// new_protocol_general_packet()/rx_thread() in newhpsdrsim.c).
//
//  - General packet (60 bytes, sent to port 1024): mostly zero - this is
//    what actually registers the sending host's address with the radio
//    for every later reply of any kind, not just command responses.
//  - Receive-specific packet (1444 bytes, sent to port 1025, resent
//    periodically): enables DDC0, sets its ADC index/sample-rate/bit-depth.
//  - High-priority packet (1444 bytes, sent to port 1027, resent
//    periodically): carries the "run" bit (byte 4, bit 0) and DDC
//    frequency tuning words (32-bit phase words, freqHz * 2^32/122880000).
//  - RX IQ data (1444 bytes, arrives from the radio on a port that
//    identifies the DDC - see kRxIqBasePort below): a 16-byte header
//    (4-byte sequence, 8 unused, then bits-per-sample/sample-count fields)
//    followed by up to 238 samples of 3-byte I + 3-byte Q.
//
// NOT yet implemented: TX, more than one receiver/DDC, PureSignal,
// diversity, wideband spectrum stream, mic/line audio, the Alex board's
// own 0/10/20/30dB attenuator or antenna-routing (EXT1/EXT2/XVTR/Bypass)
// bits - setAttenuation() is a no-op for now (unlike OldProtocolConnection's,
// which drives Hermes's own separate ADC0 step attenuator, not Alex's).
class NewProtocolConnection : public RadioConnection {
    Q_OBJECT

public:
    explicit NewProtocolConnection(QObject *parent = nullptr);

    void connectToDevice(const DiscoveredDevice &device, double rxFrequencyHz = 7100000.0) override;
    void disconnectFromDevice() override;
    bool isConnected() const override { return m_connected; }

    void setRxFrequency(double hz) override { m_rxFrequencyHz = hz; }
    double rxFrequency() const override { return m_rxFrequencyHz; }

    // No step attenuator wired up yet for Protocol 2 - see class comment.
    void setAttenuation(int db) override { Q_UNUSED(db); }
    int attenuation() const override { return 0; }

    // Alex/Apollo-compatible filter board: RX HPF/BPF and LPF bank
    // selection by tuned frequency, computed and sent every high-priority
    // packet (see RadioConnection::setFilterBoardEnabled()'s doc comment
    // and computeAlex0() in newprotocol.cpp). Off by default - only turn
    // this on if a filter board is actually wired to the radio's standard
    // Alex control connector; the exact relay-to-function mapping is fixed
    // in the radio's own firmware, not something this class controls, so a
    // self-built board only works correctly if it followed the same
    // convention a real Alex board uses.
    void setFilterBoardEnabled(bool enabled) override { m_filterBoardEnabled = enabled; }

private slots:
    void readPendingDatagrams();
    void sendPeriodicPackets();
    void reportStats();

private:
    void sendGeneralPacket();
    void sendReceiveSpecificPacket();
    void sendHighPriorityPacket();
    void parseRxIqPacket(const uchar *data, int length);

    QUdpSocket *m_socket = nullptr;
    QTimer *m_periodicTimer = nullptr;
    QTimer *m_statsTimer = nullptr;

    QHostAddress m_deviceAddress;
    bool m_connected = false;
    bool m_filterBoardEnabled = false;

    quint32 m_generalSequence = 0;
    quint32 m_receiveSpecificSequence = 0;
    quint32 m_highPrioritySequence = 0;
    double m_rxFrequencyHz = 7100000.0;

    quint64 m_packetsReceived = 0;
    quint64 m_samplesReceived = 0;
    quint64 m_lastStatsPackets = 0;
    quint64 m_lastStatsSamples = 0;
};

#endif // QHPSDR_NEWPROTOCOL_H
