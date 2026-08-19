#include "newprotocol.h"

#include <QTimer>
#include <QUdpSocket>
#include <array>
#include <cmath>
#include <fftw3.h>

namespace {

// Protocol 2 host<->radio ports (core/deskhpsdr-src/new_protocol.h). Only
// the ones this receive-only subset actually uses.
constexpr quint16 kGeneralPort = 1024;          // general packet, host -> radio
constexpr quint16 kReceiveSpecificPort = 1025;  // receive-specific packet, host -> radio
constexpr quint16 kHighPriorityPort = 1027;     // high-priority packet, host -> radio
constexpr quint16 kRxIqBasePort = 1035;         // radio -> host, +ddc index (source port)
constexpr quint16 kWidebandPort = 1027;         // radio -> host, wideband spectrum (source port)
constexpr int kMaxDdc = 4;

constexpr int kGeneralPacketSize = 60;
constexpr int kReceiveSpecificPacketSize = 1444;
constexpr int kHighPriorityPacketSize = 1444;
constexpr int kRxIqPacketSize = 1444;
constexpr int kRxIqSamplesPerPacket = 238;

// Wideband spectrum config sent in the general packet - see
// RadioConnection::setWidebandEnabled()'s doc comment: these values (512
// bins, 16-bit samples, 1 packet/frame) match new_protocol.h's/
// newhpsdrsim.c's documented *defaults*, chosen so the radio's own
// fallback matches what's sent explicitly - not derived from any working
// example. kWidebandUpdateRateMs is a free choice (no documented default),
// picked to roughly match the panadapter's own repaint cadence.
constexpr int kWidebandSampleCount = 512;
constexpr int kWidebandSampleBits = 16;
constexpr int kWidebandUpdateRateMs = 50;
constexpr int kWidebandPacketsPerFrame = 1;
// Nyquist frequency of the standard 122.88MHz HPSDR ADC clock (same
// reference clock as kPhaseWordPerHz below) - the span the wideband sweep
// covers, 0Hz to this value, one-sided (real ADC samples, not I/Q).
constexpr double kWidebandSampleRateHz = 61440000.0;

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

// ALEX0 register: RX HPF/BPF + LPF bank selection, computed from the tuned
// RX frequency. Mirrors new_protocol_high_priority()'s "default" board
// family branch in new_protocol.c (i.e. a standard Alex-compatible filter
// board - NOT the ANAN-7000/8000/Saturn band-pass-filter variant, and not
// Orion2's dual-ADC case). Bit values from core/deskhpsdr-src/alex.h.
//
// Two independent filter banks get combined into one word: RX high-pass
// filters (picked by the RX frequency directly) and TX low-pass filters
// (also picked by the RX frequency here, since - per new_protocol.c's
// comment - receive signals through Ant1/2/3 pass through the TX LPFs too
// on this board family, and this class never transmits so LPFfreq==RX
// frequency unconditionally, unlike new_protocol.c which switches to the
// DUC frequency while actually transmitting).
//
// NOT implemented: antenna routing (EXT1/EXT2/XVTR/Bypass - assumes the
// default Ant1/2/3 routing), the Alex attenuator, T/R relay, PureSignal
// bit - all only relevant once TX exists.
quint32 computeAlex0(double rxFrequencyHz) {
    quint32 alex0 = 0;

    // RX high-pass filters (alex.h: ALEX_BYPASS_HPF, ALEX_1_5MHZ_HPF,
    // ALEX_6_5MHZ_HPF, ALEX_9_5MHZ_HPF, ALEX_13MHZ_HPF, ALEX_20MHZ_HPF,
    // ALEX_6M_PREAMP).
    if (rxFrequencyHz < 1800000.0) {
        alex0 |= 0x00001000; // ALEX_BYPASS_HPF
    } else if (rxFrequencyHz < 6500000.0) {
        alex0 |= 0x00000040; // ALEX_1_5MHZ_HPF
    } else if (rxFrequencyHz < 9500000.0) {
        alex0 |= 0x00000020; // ALEX_6_5MHZ_HPF
    } else if (rxFrequencyHz < 13000000.0) {
        alex0 |= 0x00000010; // ALEX_9_5MHZ_HPF
    } else if (rxFrequencyHz < 20000000.0) {
        alex0 |= 0x00000002; // ALEX_13MHZ_HPF
    } else if (rxFrequencyHz < 50000000.0) {
        alex0 |= 0x00000004; // ALEX_20MHZ_HPF
    } else {
        alex0 |= 0x00000008; // ALEX_6M_PREAMP
    }

    // TX low-pass filters (alex.h: ALEX_160/80/60_40/30_20/17_15/12_10_LPF,
    // ALEX_6_BYPASS_LPF).
    if (rxFrequencyHz > 35600000.0) {
        alex0 |= 0x20000000; // ALEX_6_BYPASS_LPF
    } else if (rxFrequencyHz > 24000000.0) {
        alex0 |= 0x40000000; // ALEX_12_10_LPF
    } else if (rxFrequencyHz > 16500000.0) {
        alex0 |= 0x80000000; // ALEX_17_15_LPF
    } else if (rxFrequencyHz > 8000000.0) {
        alex0 |= 0x00100000; // ALEX_30_20_LPF
    } else if (rxFrequencyHz > 5000000.0) {
        alex0 |= 0x00200000; // ALEX_60_40_LPF
    } else if (rxFrequencyHz > 2500000.0) {
        alex0 |= 0x00400000; // ALEX_80_LPF
    } else {
        alex0 |= 0x00800000; // ALEX_160_LPF
    }

    return alex0;
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

    // Real-input FFT for the wideband burst - see setWidebandEnabled()'s
    // doc comment. Reuses a plain complex-to-complex plan (imaginary part
    // fed as 0) rather than a dedicated real-to-complex FFTW plan, purely
    // for code-path simplicity - CPU cost is irrelevant at 512 points/20Hz.
    m_widebandFftIn.assign(kWidebandSampleCount, {0.0f, 0.0f});
    m_widebandFftOut.assign(kWidebandSampleCount, {0.0f, 0.0f});
    m_widebandFftPlan =
        fftwf_plan_dft_1d(kWidebandSampleCount, reinterpret_cast<fftwf_complex *>(m_widebandFftIn.data()),
                           reinterpret_cast<fftwf_complex *>(m_widebandFftOut.data()), FFTW_FORWARD, FFTW_ESTIMATE);
}

NewProtocolConnection::~NewProtocolConnection() {
    if (m_widebandFftPlan) {
        fftwf_destroy_plan(static_cast<fftwf_plan>(m_widebandFftPlan));
    }
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
    // All per-stream port-override fields (bytes 5-22, except the wideband
    // ones written below) are left zero, so the radio uses its documented
    // defaults (new_protocol.h) - exactly what deskHPSDR itself does.
    if (m_widebandEnabled) {
        // byte23=enable, bytes24-25=sample count (BE16), byte26=bits/sample,
        // byte27=update rate (ms), byte28=packets/frame - see
        // kWidebandSampleCount et al.'s comment for how unverified this is.
        buf[23] = 0x01;
        putU16BE(&buf[24], quint16(kWidebandSampleCount));
        buf[26] = uchar(kWidebandSampleBits);
        buf[27] = uchar(kWidebandUpdateRateMs);
        buf[28] = uchar(kWidebandPacketsPerFrame);
    }
    buf[37] = 0x08; // phase word (not frequency), matches new_protocol_general()
    buf[38] = 0x01; // enable hardware timer
    // buf[58] (PA enable) stays 0 - no PA/TX yet. buf[59] bit 0 is the
    // ALEX0 register enable flag - without it the radio ignores whatever
    // alex0 bits sendHighPriorityPacket() sends. ALEX1 (bit 1) stays off -
    // that's the second filter board on dual-ADC boards this class doesn't
    // support.
    if (m_filterBoardEnabled) {
        buf[59] = 0x01;
    }
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
    if (m_filterBoardEnabled) {
        // ALEX0 at bytes 1432-1435 (new_protocol.c:
        // high_priority_buffer_to_radio[1432..1435]) - only meaningful if
        // the general packet's ALEX0 enable bit is also set.
        putU32BE(&buf[1432], computeAlex0(m_rxFrequencyHz));
    }
    m_socket->writeDatagram(reinterpret_cast<const char *>(buf.data()), qint64(buf.size()), m_deviceAddress,
                             kHighPriorityPort);
}

void NewProtocolConnection::sendPeriodicPackets() {
    // Also resends the general packet (carries the ALEX0-enable flag) -
    // needed so toggling setFilterBoardEnabled() mid-session takes effect
    // without a full reconnect, not just at connectToDevice()'s one-shot
    // initial send.
    sendGeneralPacket();
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
        } else if (senderPort == kWidebandPort && m_widebandEnabled) {
            parseWidebandPacket(reinterpret_cast<const uchar *>(buffer.constData()), buffer.size());
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

void NewProtocolConnection::parseWidebandPacket(const uchar *data, int length) {
    // Confirmed 2026-08-19 against real hardware (see setWidebandEnabled()'s
    // doc comment): 4-byte sequence header, then kWidebandSampleCount
    // consecutive big-endian SIGNED 16-bit raw time-domain ADC samples -
    // NOT a precomputed magnitude spectrum as originally assumed (real
    // captures showed a near-zero mean and roughly symmetric +/- swing,
    // the opposite of what a non-negative magnitude/power array would
    // look like). This class runs its own windowed FFT on each burst,
    // the same idea as SpectrumAnalyzer's for the narrowband IQ stream.
    const int headerSize = 4;
    const int bytesPerSample = kWidebandSampleBits / 8;
    const int needed = headerSize + kWidebandSampleCount * bytesPerSample;
    if (length < needed) {
        return;
    }

    // Hann window, matching SpectrumAnalyzer's choice, then real samples
    // (normalized to [-1,1]) into the complex FFT input with imaginary=0.
    const uchar *p = data + headerSize;
    for (int i = 0; i < kWidebandSampleCount; ++i, p += bytesPerSample) {
        const qint16 raw = qint16((p[0] << 8) | p[1]);
        const float windowed = float(0.5 * (1.0 - std::cos(2.0 * M_PI * i / (kWidebandSampleCount - 1))));
        m_widebandFftIn[size_t(i)] = {(float(raw) / 32768.0f) * windowed, 0.0f};
    }

    fftwf_execute(static_cast<fftwf_plan>(m_widebandFftPlan));

    // Real input -> conjugate-symmetric output: only bins 0 (DC) through
    // N/2 (Nyquist) are unique, N/2+1 bins total spanning the one-sided
    // 0Hz..kWidebandSampleRateHz range this signal emits.
    const int uniqueBins = kWidebandSampleCount / 2 + 1;
    QVector<float> magnitudesDb;
    magnitudesDb.reserve(uniqueBins);
    constexpr float kFloorDb = -180.0f;
    for (int n = 0; n < uniqueBins; ++n) {
        const float mag = std::abs(m_widebandFftOut[size_t(n)]) / float(kWidebandSampleCount);
        magnitudesDb.append(mag > 1e-9f ? 20.0f * std::log10(mag) : kFloorDb);
    }
    emit wideSpectrumReady(magnitudesDb, kWidebandSampleRateHz);
}

void NewProtocolConnection::reportStats() {
    const quint64 samples = m_samplesReceived - m_lastStatsSamples;
    m_lastStatsPackets = m_packetsReceived;
    m_lastStatsSamples = m_samplesReceived;
    emit statsUpdated(m_packetsReceived, m_samplesReceived, double(samples));
}
