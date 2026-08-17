#ifndef QHPSDR_RXAUDIO_H
#define QHPSDR_RXAUDIO_H

#include <QObject>
#include <QVector>
#include <vector>

// Demodulation modes, matching WDSP's `enum rxaMode` (core/wdsp-2.00/RXA.h).
// Duplicated here (rather than including RXA.h, which drags in WDSP's
// internal comm.h world) since this is the only piece of that enum any
// Qt/C++ code needs.
enum class RxMode {
    LSB = 0,
    USB = 1,
    DSB = 2,
    CWL = 3,
    CWU = 4,
    FM = 5,
    AM = 6,
    DIGU = 7,
    SPEC = 8,
    DIGL = 9,
    SAM = 10,
    DRM = 11,
    WBFM = 12,
};

// Qt wrapper around a WDSP RXA receive channel (core/wdsp-2.00), replacing
// deskHPSDR's WDSP setup in core/deskhpsdr-src/receiver.c (OpenChannel +
// per-block fexchange0 calls). Feed it normalized I/Q samples one at a
// time; once WDSP's block size worth have accumulated it runs them through
// WDSP and emits demodulated audio.
//
// open() is asynchronous: the underlying WDSP OpenChannel() call plans
// FFTW filters, which is slow the first time any given size is needed.
// open() calls WDSPwisdom() first to load (or, once ever, build and cache
// to disk) a wisdom file, so every run after the very first one is fast -
// see buildingWisdom(). Both run on a worker thread so this can't freeze
// the GUI; isOpen() flips to true and opened() fires once it's done.
// feedSample() silently drops samples until then.
//
// Not yet implemented: noise blankers (create_anbEXT/create_nobEXT in the
// original), meters, squelch, AGC tuning - WDSP defaults apply.
class RxAudioChannel : public QObject {
    Q_OBJECT

public:
    explicit RxAudioChannel(QObject *parent = nullptr);
    ~RxAudioChannel() override;

    // channel must be unique among concurrently-open RxAudioChannels (WDSP
    // indexes internal state by this number). Returns once the (slow)
    // OpenChannel() call has been kicked off on a worker thread; isOpen()
    // and feedSample() aren't usable until opened() fires.
    void open(int channel, int inputSampleRate);
    void close();
    bool isOpen() const { return m_open; }

    void setMode(RxMode mode);

    // i, q normalized to [-1, 1] (e.g. a 24-bit ADC sample / 2^23). No-op
    // while the channel is still opening or closed.
    void feedSample(double i, double q);

signals:
    // Interleaved stereo (L, R, L, R, ...), normalized to roughly [-1, 1].
    void audioBlockReady(QVector<float> interleavedStereo);
    // Fired once open() has finished and feedSample()/audioBlockReady()
    // are live. Never fires if close() is called before it completes.
    void opened();
    // Fired instead of (before) opened() completing quickly, when open()
    // notices no cached FFTW wisdom file exists yet and is about to build
    // one - a one-time cost (can take several minutes) that's cached to
    // disk afterward, see WDSPwisdom() in core/wdsp-2.00/wisdom.c.
    void buildingWisdom();

private:
    void processBlock();
    void finishOpen();
    void applyDefaultPassband(RxMode mode);

    bool m_open = false;
    bool m_opening = false;
    bool m_closePending = false;
    int m_channel = 0;
    static constexpr int kBlockSize = 1024; // matches deskHPSDR's default rx->buffer_size

    std::vector<double> m_inBuffer;  // interleaved I/Q, 2*kBlockSize doubles
    std::vector<double> m_outBuffer; // interleaved L/R, 2*kBlockSize doubles
    int m_fillCount = 0;
};

#endif // QHPSDR_RXAUDIO_H
