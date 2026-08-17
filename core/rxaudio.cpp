#include "rxaudio.h"

extern "C" {
#include "wdsp.h"
}

RxAudioChannel::RxAudioChannel(QObject *parent) : QObject(parent) {}

RxAudioChannel::~RxAudioChannel() {
    close();
}

void RxAudioChannel::open(int channel, int inputSampleRate) {
    close();

    m_channel = channel;
    m_inBuffer.assign(2 * kBlockSize, 0.0);
    m_outBuffer.assign(2 * kBlockSize, 0.0);
    m_fillCount = 0;

    OpenChannel(m_channel,
                kBlockSize,      // in_size
                2048,            // dsp_size
                inputSampleRate, // input samplerate
                48000,           // dsp rate
                48000,           // output samplerate
                0,               // type: 0 = RXA
                1,               // state: running
                0.010, 0.025, 0.0, 0.010, // delay/slew up/down
                1);              // block fexchange0 until output is ready

    // setMode() below is a no-op while m_open is still false, so this must
    // come before any Set* calls that need to take effect immediately.
    m_open = true;

    // Baseline config deskHPSDR always applies regardless of mode
    // (core/deskhpsdr-src/receiver.c, right after OpenChannel).
    SetRXABandpassWindow(m_channel, 1); // 7-term BlackmanHarris
    SetRXABandpassRun(m_channel, 1);
    SetRXAAMDSBMode(m_channel, 0);
    SetRXAPanelRun(m_channel, 1);

    // AGC defaults (deskHPSDR's receiver.c: rx->agc/agc_gain/agc_slope
    // defaults). Without an explicit mode, WDSP's AGC stays at whatever
    // create_rxa() zero-initializes it to, which mutes the output.
    SetRXAAGCMode(m_channel, 3 /* AGC_MEDIUM */);
    SetRXAAGCTop(m_channel, 80.0);
    SetRXAAGCSlope(m_channel, 35);
    SetRXAAGCAttack(m_channel, 2);
    SetRXAAGCHang(m_channel, 0);
    SetRXAAGCDecay(m_channel, 50);
    SetRXAAGCHangThreshold(m_channel, 100);

    setMode(RxMode::AM);
}

void RxAudioChannel::close() {
    if (!m_open) {
        return;
    }
    CloseChannel(m_channel);
    m_open = false;
}

void RxAudioChannel::setMode(RxMode mode) {
    if (m_open) {
        SetRXAMode(m_channel, static_cast<int>(mode));
    }
}

void RxAudioChannel::feedSample(double i, double q) {
    if (!m_open) {
        return;
    }
    m_inBuffer[2 * m_fillCount] = i;
    m_inBuffer[2 * m_fillCount + 1] = q;
    if (++m_fillCount >= kBlockSize) {
        processBlock();
        m_fillCount = 0;
    }
}

void RxAudioChannel::processBlock() {
    int error = 0;
    fexchange0(m_channel, m_inBuffer.data(), m_outBuffer.data(), &error);

    QVector<float> out(int(m_outBuffer.size()));
    for (size_t i = 0; i < m_outBuffer.size(); ++i) {
        out[int(i)] = float(m_outBuffer[i]);
    }
    emit audioBlockReady(out);
}
