#include "spectrumanalyzer.h"

#include <cmath>

#include <fftw3.h>

SpectrumAnalyzer::SpectrumAnalyzer(QObject *parent) : QObject(parent) {
    m_buffer.assign(kFftSize, {0.0f, 0.0f});
    m_fftIn.assign(kFftSize, {0.0f, 0.0f});
    m_fftOut.assign(kFftSize, {0.0f, 0.0f});

    // Hann window - a reasonable general-purpose choice, trades main-lobe
    // width against sidelobe suppression better than a rectangular window.
    m_window.resize(kFftSize);
    for (int n = 0; n < kFftSize; ++n) {
        m_window[size_t(n)] = float(0.5 * (1.0 - std::cos(2.0 * M_PI * n / (kFftSize - 1))));
    }

    m_plan = fftwf_plan_dft_1d(kFftSize, reinterpret_cast<fftwf_complex *>(m_fftIn.data()),
                                reinterpret_cast<fftwf_complex *>(m_fftOut.data()), FFTW_FORWARD, FFTW_ESTIMATE);
}

SpectrumAnalyzer::~SpectrumAnalyzer() {
    if (m_plan) {
        fftwf_destroy_plan(static_cast<fftwf_plan>(m_plan));
    }
}

void SpectrumAnalyzer::feedSample(double i, double q) {
    // I/Q swapped, not {i, q} - matches core/wdsp-2.00/analyzer.c's
    // Spectrum0(): "Ipointer[n] = pbuff[2n+1]; Qpointer[n] = pbuff[2n+0];"
    // against an iq_input_buffer packed as [i,q,i,q,...] (see
    // core/deskhpsdr-src/receiver.c's rx_add_iq_samples()). WDSP's own
    // analyzer - what real deskHPSDR's panadapter runs on - deliberately
    // feeds its FFT the opposite sense from what SetRXAMode's demodulator
    // gets, to correctly orient the display against real HPSDR hardware's
    // baseband sign convention. Confirmed via hpsdrsim.c's synthetic test
    // tone (do_tone: off=signal_freq-tuned_freq, tonedelta=-2*pi*off/fs,
    // i.e. a signal ABOVE the tuned frequency produces a NEGATIVE baseband
    // frequency on the wire) - the "textbook" downconversion sign this
    // analyzer originally assumed is backwards for this hardware.
    m_buffer[size_t(m_fillCount)] = {float(q), float(i)};
    if (++m_fillCount >= kFftSize) {
        computeFrame();
        m_fillCount = 0;
    }
}

void SpectrumAnalyzer::computeFrame() {
    for (int n = 0; n < kFftSize; ++n) {
        m_fftIn[size_t(n)] = m_buffer[size_t(n)] * m_window[size_t(n)];
    }

    fftwf_execute(static_cast<fftwf_plan>(m_plan));

    QVector<float> db(kFftSize);
    constexpr float kFloorDb = -180.0f;
    for (int n = 0; n < kFftSize; ++n) {
        // fftshift: negative frequencies (upper half of the raw FFT output)
        // first, so index 0 is the bottom of the passband.
        const int src = (n + kFftSize / 2) % kFftSize;
        const float mag = std::abs(m_fftOut[size_t(src)]) / float(kFftSize);
        db[n] = mag > 1e-9f ? 20.0f * std::log10(mag) : kFloorDb;
    }

    emit spectrumReady(db);
}
