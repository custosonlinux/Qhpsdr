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

// Matches deskHPSDR's `enum _agc_enum` (core/deskhpsdr-src/agc.h) and
// WDSP's SetRXAAGCMode() mode parameter (core/wdsp-2.00/wcpAGC.c) 1:1 -
// there is no separate "auto" mode, these 5 (with their own fixed
// attack/hang/decay presets, see RxAudioChannel::setAgcMode()) are the
// complete set deskHPSDR itself offers.
enum class AgcMode {
    Off = 0,
    Long = 1,
    Slow = 2,
    Medium = 3,
    Fast = 4,
};

// Matches deskHPSDR's rx->nb (core/deskhpsdr-src/receiver.c/noise_menu.c):
// "NONE"/"NB"/"NB2" - two different impulse-noise-blanker algorithms
// (WDSP's EXTANB vs EXTNOB), mutually exclusive.
enum class NoiseBlankerMode {
    Off = 0,
    Nb1 = 1,
    Nb2 = 2,
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
// Not yet implemented: squelch, noise reduction (ANR/EMNR - separate from
// the noise blanker above), NB2's alternate sub-modes, and user-adjustable
// noise-blanker/AGC-slope parameters (deskHPSDR exposes those via a
// separate settings dialog this project doesn't have yet).
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

    // Applies the mode's default passband width (see core/filtertable.h)
    // in addition to switching the demodulator.
    void setMode(RxMode mode);

    // For picking a specific named filter preset instead of the mode
    // default - see core/filtertable.h's filtersForMode(). No-op while
    // the channel is closed/still opening.
    void setPassband(double lowHz, double highHz);

    // AF (output volume) gain in dB, -40..0 - core/deskhpsdr-src/
    // receiver.c's rx_set_af_gain(): converts to a 0..1 amplitude
    // (0 at/below -39.5dB, 1 above 0dB, pow(10, 0.05*dB) between) applied
    // via WDSP's RXA "panel" gain stage. No-op while closed/opening.
    void setAfGain(double dB);

    // AGC mode + gain ("Top"), matching deskHPSDR's rx_set_agc()
    // (core/deskhpsdr-src/receiver.c) exactly: each mode has its own
    // fixed attack/hang/decay/hang-threshold preset (no separate "auto"
    // mode - Off/Long/Slow/Medium/Fast is the complete set), while Top
    // (the AGC gain ceiling, -20..120dB) applies independently of mode.
    // No-op while closed/opening; setAgcTop() still remembers the value
    // to apply once open.
    void setAgcMode(AgcMode mode);
    void setAgcTop(double dB);

    // Impulse noise blanker, applied in-place on the raw I/Q before
    // fexchange0() (matches deskHPSDR's rx_full_buffer() - NOT a one-time
    // open-time setting like AGC, since it runs every block). No-op while
    // closed/opening; feedSample()/processBlock() apply it once open.
    void setNoiseBlanker(NoiseBlankerMode mode);

    // i, q normalized to [-1, 1] (e.g. a 24-bit ADC sample / 2^23). No-op
    // while the channel is still opening or closed.
    void feedSample(double i, double q);

signals:
    // Interleaved stereo (L, R, L, R, ...), normalized to roughly [-1, 1].
    void audioBlockReady(QVector<float> interleavedStereo);
    // WDSP's own S-meter reading (RXA_S_AV, dBm), emitted once per
    // processed block alongside audioBlockReady - see
    // core/deskhpsdr-src/receiver.c's rx_get_smeter().
    void meterUpdated(double dbm);
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

    bool m_open = false;
    bool m_opening = false;
    bool m_closePending = false;
    int m_channel = 0;
    int m_inputSampleRate = 48000; // set by open(); create_anbEXT()/create_nobEXT() need it in finishOpen()
    static constexpr int kBlockSize = 1024; // matches deskHPSDR's default rx->buffer_size

    std::vector<double> m_inBuffer;  // interleaved I/Q, 2*kBlockSize doubles
    std::vector<double> m_outBuffer; // interleaved L/R, 2*kBlockSize doubles
    int m_fillCount = 0;

    double m_agcTopDb = 80.0; // core/deskhpsdr-src/receiver.c's rx->agc_gain default
    NoiseBlankerMode m_nbMode = NoiseBlankerMode::Off;
};

#endif // QHPSDR_RXAUDIO_H
