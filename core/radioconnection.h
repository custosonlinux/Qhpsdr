#ifndef QHPSDR_RADIOCONNECTION_H
#define QHPSDR_RADIOCONNECTION_H

#include <QObject>
#include <QVector>

#include "discovered.h"

// Common interface implemented by both OldProtocolConnection (HPSDR
// "Protocol 1" / METIS) and NewProtocolConnection (HPSDR "Protocol 2"),
// so MainWindow can wire up whichever one a given DiscoveredDevice needs
// without branching its signal/slot plumbing on the protocol - only
// connectToDevice() at the call site needs to know which concrete class
// to construct.
class RadioConnection : public QObject {
    Q_OBJECT

public:
    explicit RadioConnection(QObject *parent = nullptr) : QObject(parent) {}

    virtual void connectToDevice(const DiscoveredDevice &device, double rxFrequencyHz = 7100000.0) = 0;
    virtual void disconnectFromDevice() = 0;
    virtual bool isConnected() const = 0;

    virtual void setRxFrequency(double hz) = 0;
    virtual double rxFrequency() const = 0;

    // Standard HPSDR step attenuator (0-31 dB). Protocol 2 devices without
    // one (or that haven't wired it up yet) can implement this as a no-op -
    // see NewProtocolConnection.
    virtual void setAttenuation(int db) = 0;
    virtual int attenuation() const = 0;

    // Alex/Apollo-compatible filter board (RX HPF/BPF + LPF banks, switched
    // by tuned frequency). Protocol 1 (OldProtocolConnection) is a no-op:
    // the Hermes firmware decodes the filter bands itself from the DDC
    // frequency it already receives, without any extra host-side bits, in
    // normal (non-manual-override) operation - see the comment in
    // oldprotocol.cpp. Protocol 2 (NewProtocolConnection) has to compute
    // and send those bits itself every high-priority packet - see
    // new_protocol_high_priority()'s ALEX0/ALEX1 computation in
    // core/deskhpsdr-src/new_protocol.c, which this mirrors for the
    // "standard" (non-ANAN7000/Orion2/Saturn) board family.
    virtual void setFilterBoardEnabled(bool enabled) = 0;

signals:
    void connected();
    void disconnected();
    // Emitted roughly once a second with running totals.
    void statsUpdated(quint64 packetsReceived, quint64 samplesReceived, double approxSampleRateHz);
    void errorOccurred(const QString &message);
    // One sub-frame's worth of samples, interleaved I/Q normalized to
    // [-1, 1]. Sub-frame size differs by protocol (63 samples for
    // Protocol 1's 1032-byte METIS frame, 238 for Protocol 2's 1444-byte
    // RX IQ frame) - callers consume this as a plain stream, not
    // frame-aligned, so the difference doesn't matter downstream.
    void iqSamplesReady(QVector<double> interleavedIQ);
};

#endif // QHPSDR_RADIOCONNECTION_H
