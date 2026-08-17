#ifndef QHPSDR_SPECTRUMANALYZER_H
#define QHPSDR_SPECTRUMANALYZER_H

#include <QObject>
#include <QVector>
#include <complex>
#include <vector>

// Computes a windowed FFT of the raw receive I/Q stream for the panadapter
// and waterfall. Not part of deskHPSDR's C core (its panadapter/waterfall
// draw from WDSP's built-in analyzer, core/wdsp-2.00/analyzer.c) - this is
// a small standalone FFTW-based analyzer instead, since wiring the display
// path into WDSP's analyzer API is a separate task from getting a
// panadapter on screen at all.
class SpectrumAnalyzer : public QObject {
    Q_OBJECT

public:
    explicit SpectrumAnalyzer(QObject *parent = nullptr);
    ~SpectrumAnalyzer() override;

    static constexpr int kFftSize = 2048;

    // i, q normalized to [-1, 1].
    void feedSample(double i, double q);

signals:
    // kFftSize values in dBFS-ish units (0 dB = full-scale complex sinusoid),
    // frequency-centered: index 0 = bottom of the receive passband (most
    // negative frequency), index kFftSize-1 = top. Emitted once per
    // non-overlapping FFT frame (~23/s at 48 kHz).
    void spectrumReady(QVector<float> magnitudesDb);

private:
    void computeFrame();

    std::vector<std::complex<float>> m_buffer;
    std::vector<float> m_window;
    int m_fillCount = 0;

    // Opaque to avoid pulling <fftw3.h> into consumers of this header.
    void *m_plan = nullptr;
    std::vector<std::complex<float>> m_fftIn;
    std::vector<std::complex<float>> m_fftOut;
};

#endif // QHPSDR_SPECTRUMANALYZER_H
