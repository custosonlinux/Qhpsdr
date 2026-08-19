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

    // Full-band ("wideband") ADC spectrum, independent of the tuned DDC's
    // own zoomed panadapter view - covers 0Hz to the ADC's Nyquist
    // frequency in one sweep. Protocol 1 has no such feature at all (no-op
    // on OldProtocolConnection). Protocol 2's wire format for this is
    // UNVERIFIED against any working reference: deskHPSDR's own
    // new_protocol.c never actually sends/enables it (the general packet
    // fields exist but are never written), and even the Saturn-specific
    // path is commented "P2 - not yet implemented" throughout
    // saturnregisters.c. NewProtocolConnection's implementation is a
    // best-effort construction from the general packet's documented field
    // *names* (new_protocol.h) and precedent from every other Protocol 2
    // packet type (4-byte sequence header) - needs real-hardware
    // confirmation, most likely the dB scaling in particular.
    virtual void setWidebandEnabled(bool enabled) = 0;

    // Second, independently-tuned receiver (DDC1) - Protocol 2 only
    // (OldProtocolConnection no-ops these, same pattern as
    // setFilterBoardEnabled()/setWidebandEnabled() above). A plain
    // single-ADC Hermes fully supports two independently-tuned DDCs
    // reading the same physical ADC - see NewProtocolConnection's
    // implementation for the confirmed wire-format details. Starts
    // disabled: no DDC1 traffic is requested, and iqSamplesReady2() never
    // fires, until this is turned on.
    virtual void setRx2Enabled(bool enabled) = 0;
    virtual void setRxFrequency2(double hz) = 0;
    virtual double rxFrequency2() const = 0;

    // Per-DDC sample rate in Hz (48000/96000/192000/384000/768000/
    // 1536000 on a Hermes-class board) - each DDC has its own independent
    // decimator, so RX1/RX2 can legitimately run at different rates
    // simultaneously off the same physical ADC. Protocol 1
    // (OldProtocolConnection) is a no-op for now - fixed at 48kHz, same
    // as setAttenuation()'s "not wired up yet" precedent. Protocol 2
    // sends the chosen rate in the receive-specific packet's per-DDC
    // sample-rate field - see NewProtocolConnection.
    virtual void setRxSampleRate(int hz) = 0;
    virtual int rxSampleRate() const = 0;
    virtual void setRxSampleRate2(int hz) = 0;
    virtual int rxSampleRate2() const = 0;

    // ADC0 dither/random-bit generators - real Protocol 2 receive-specific
    // packet fields (bytes 5/6, one bit per ADC index) that this class
    // previously left at zero. Dither reduces quantization distortion at
    // low signal levels at the cost of a slightly higher noise floor;
    // random adds a dithering PRBS to further decorrelate quantization
    // error - both are standard ADC linearization techniques, off by
    // default to match this project's previous (implicit) behavior.
    // Shared across both DDCs (one physical ADC0) - Protocol 1 is a no-op,
    // same "not wired up yet" precedent as setAttenuation().
    virtual void setAdcDither(bool enabled) = 0;
    virtual bool adcDither() const = 0;
    virtual void setAdcRandom(bool enabled) = 0;
    virtual bool adcRandom() const = 0;

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

    // Full-band spectrum magnitudes in dB, one-sided (0Hz..sampleRateHz,
    // not centered like the DDC panadapter's complex-baseband view) - see
    // setWidebandEnabled(). Never emitted by OldProtocolConnection.
    void wideSpectrumReady(QVector<float> magnitudesDb, double sampleRateHz);

    // DDC1's own I/Q stream - see setRx2Enabled(). Never emitted by
    // OldProtocolConnection or while RX2 is disabled.
    void iqSamplesReady2(QVector<double> interleavedIQ);
};

#endif // QHPSDR_RADIOCONNECTION_H
