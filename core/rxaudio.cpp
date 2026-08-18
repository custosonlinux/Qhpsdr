#include "rxaudio.h"

#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QStandardPaths>
#include <cmath>
#include <thread>

#include "filtertable.h"

extern "C" {
#include "wdsp.h"
}

namespace {
// Trailing separator required: WDSPwisdom() does a plain strcpy+strncat of
// "wdspWisdom01" onto whatever is passed in (core/wdsp-2.00/wisdom.c).
QString wisdomDirectory() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir.isEmpty() ? QString() : dir + QLatin1Char('/');
}
} // namespace

RxAudioChannel::RxAudioChannel(QObject *parent) : QObject(parent) {}

RxAudioChannel::~RxAudioChannel() {
    close();
}

void RxAudioChannel::open(int channel, int inputSampleRate) {
    close();

    m_channel = channel;
    m_inputSampleRate = inputSampleRate;
    m_inBuffer.assign(2 * kBlockSize, 0.0);
    m_outBuffer.assign(2 * kBlockSize, 0.0);
    m_fillCount = 0;
    m_opening = true;
    m_closePending = false;

    // OpenChannel() plans FFTW filters, which is slow the first time any
    // given size is needed - so load (or, once ever, build and cache to
    // disk) FFTW wisdom first (see the class comment), and run both on a
    // worker thread. Plain C with its own internal locking
    // (ch[channel].csDSP/csEXCH), so calling it off the Qt object's thread
    // is safe; only touching `this` afterward needs to happen back on our
    // own thread, hence the invokeMethod hops.
    std::thread([this, channel, inputSampleRate]() {
        const QString dir = wisdomDirectory();
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            if (!QFile::exists(dir + QStringLiteral("wdspWisdom01"))) {
                QMetaObject::invokeMethod(this, [this]() { emit buildingWisdom(); }, Qt::QueuedConnection);
            }
            QByteArray dirBytes = dir.toLocal8Bit();
            WDSPwisdom(dirBytes.data());
        }

        OpenChannel(channel,
                    kBlockSize,      // in_size
                    2048,            // dsp_size
                    inputSampleRate, // input samplerate
                    48000,           // dsp rate
                    48000,           // output samplerate
                    0,               // type: 0 = RXA
                    1,               // state: running
                    0.010, 0.025, 0.0, 0.010, // delay/slew up/down
                    1);              // block fexchange0 until output is ready
        QMetaObject::invokeMethod(this, [this]() { finishOpen(); }, Qt::QueuedConnection);
    }).detach();
}

void RxAudioChannel::finishOpen() {
    m_opening = false;
    if (m_closePending) {
        m_closePending = false;
        m_open = true; // let close() below actually tear it down
        close();
        return;
    }

    // Unlike RXA (created by OpenChannel() above), WDSP's EXTANB/EXTNOB
    // (the noise blanker) need their own separate creation call before any
    // Set*/x*EXT call touches them - core/deskhpsdr-src/receiver.c calls
    // both right after OpenChannel(), with these exact literal defaults
    // (immediately overridden by setNoiseBlanker() below via
    // rx_set_noise()'s real Tau/Hangtime/Advtime/Threshold values -
    // "backtau" has no runtime setter and keeps whatever's passed here).
    // Without this, setNoiseBlanker()'s Set* calls dereference an
    // uninitialized ANB/NOB pointer and crash.
    create_anbEXT(m_channel, 1, kBlockSize, m_inputSampleRate, 0.0001, 0.0001, 0.0001, 0.05, 20);
    create_nobEXT(m_channel, 1, 0, kBlockSize, m_inputSampleRate, 0.0001, 0.0001, 0.0001, 0.05, 20);

    // setMode() below is a no-op while m_open is still false, so this must
    // come before any Set* calls that need to take effect immediately.
    m_open = true;

    // Baseline config deskHPSDR always applies regardless of mode
    // (core/deskhpsdr-src/receiver.c, right after OpenChannel).
    SetRXABandpassWindow(m_channel, 1); // 7-term BlackmanHarris
    SetRXABandpassRun(m_channel, 1);
    SetRXAAMDSBMode(m_channel, 0);
    SetRXAPanelRun(m_channel, 1);

    // Without an explicit mode, WDSP's AGC stays at whatever create_rxa()
    // zero-initializes it to, which mutes the output.
    setAgcMode(AgcMode::Medium);
    setNoiseBlanker(NoiseBlankerMode::Off);
    setNoiseReduction(NoiseReductionMode::Off);
    setAutoNotch(false);
    setSpectralNoiseBlanker(false);

    setMode(RxMode::AM);
    emit opened();
}

void RxAudioChannel::close() {
    if (m_opening) {
        // OpenChannel() is still running on the worker thread; tear down
        // once finishOpen() lands instead of racing it.
        m_closePending = true;
        return;
    }
    if (!m_open) {
        return;
    }
    CloseChannel(m_channel);
    destroy_anbEXT(m_channel);
    destroy_nobEXT(m_channel);
    m_open = false;
}

void RxAudioChannel::setMode(RxMode mode) {
    if (!m_open) {
        return;
    }
    SetRXAMode(m_channel, static_cast<int>(mode));
    const auto filters = filtersForMode(mode);
    const int idx = defaultFilterIndexForMode(mode);
    if (idx >= 0 && idx < filters.size()) {
        setPassband(filters[idx].low, filters[idx].high);
    }
}

void RxAudioChannel::setPassband(double lowHz, double highHz) {
    if (!m_open) {
        return;
    }
    RXASetPassband(m_channel, lowHz, highHz);
}

void RxAudioChannel::setAfGain(double dB) {
    if (!m_open) {
        return;
    }
    double amplitude;
    if (dB <= -39.5) {
        amplitude = 0.0;
    } else if (dB > 0.0) {
        amplitude = 1.0;
    } else {
        amplitude = std::pow(10.0, 0.05 * dB);
    }
    SetRXAPanelGain1(m_channel, amplitude);
}

void RxAudioChannel::setAgcMode(AgcMode mode) {
    if (!m_open) {
        return;
    }
    SetRXAAGCMode(m_channel, static_cast<int>(mode));
    if (mode != AgcMode::Off) {
        // Per-mode attack/hang/decay/hang-threshold presets, exactly
        // matching core/deskhpsdr-src/receiver.c's rx_set_agc() - deskHPSDR
        // makes no further calls at all for AGC_OFF.
        SetRXAAGCAttack(m_channel, 2);
        switch (mode) {
        case AgcMode::Long:
            SetRXAAGCHang(m_channel, 2000);
            SetRXAAGCDecay(m_channel, 2000);
            SetRXAAGCHangThreshold(m_channel, 0); // rx->agc_hang_threshold default
            break;
        case AgcMode::Slow:
            SetRXAAGCHang(m_channel, 1000);
            SetRXAAGCDecay(m_channel, 500);
            SetRXAAGCHangThreshold(m_channel, 0);
            break;
        case AgcMode::Fast:
            SetRXAAGCHang(m_channel, 0);
            SetRXAAGCDecay(m_channel, 50);
            SetRXAAGCHangThreshold(m_channel, 100);
            break;
        case AgcMode::Medium:
        default:
            SetRXAAGCHang(m_channel, 0);
            SetRXAAGCDecay(m_channel, 250);
            SetRXAAGCHangThreshold(m_channel, 100);
            break;
        }
        SetRXAAGCSlope(m_channel, 35); // rx->agc_slope default; no dedicated UI yet
        SetRXAAGCTop(m_channel, m_agcTopDb);
    }
}

void RxAudioChannel::setAgcTop(double dB) {
    m_agcTopDb = dB;
    if (m_open) {
        SetRXAAGCTop(m_channel, dB);
    }
}

void RxAudioChannel::setNoiseBlanker(NoiseBlankerMode mode) {
    m_nbMode = mode;
    if (!m_open) {
        return;
    }
    // core/deskhpsdr-src/receiver.c's rx_set_noise() defaults - not yet
    // user-adjustable here (deskHPSDR exposes them via a separate Noise
    // settings dialog, not the toolbar).
    constexpr double kTau = 0.00001;
    constexpr double kHang = 0.00001;
    constexpr double kAdv = 0.00001;
    constexpr double kThreshold = 4.95;
    SetEXTANBTau(m_channel, kTau);
    SetEXTANBHangtime(m_channel, kHang);
    SetEXTANBAdvtime(m_channel, kAdv);
    SetEXTANBThreshold(m_channel, kThreshold);
    SetEXTANBRun(m_channel, mode == NoiseBlankerMode::Nb1 ? 1 : 0);
    SetEXTNOBTau(m_channel, kTau);
    SetEXTNOBHangtime(m_channel, kHang);
    SetEXTNOBAdvtime(m_channel, kAdv);
    SetEXTNOBThreshold(m_channel, kThreshold);
    SetEXTNOBMode(m_channel, 0); // NB2 submode ("Zero") - not exposed yet
    SetEXTNOBRun(m_channel, mode == NoiseBlankerMode::Nb2 ? 1 : 0);
}

void RxAudioChannel::setNoiseReduction(NoiseReductionMode mode) {
    m_nrMode = mode;
    if (!m_open) {
        return;
    }
    // Disable both before switching, matching core/deskhpsdr-src/
    // receiver.c's rx_set_noise() ordering - avoids a transient overlap
    // where both run briefly during a switch.
    SetRXAANRRun(m_channel, 0);
    SetRXAEMNRRun(m_channel, 0);
    switch (mode) {
    case NoiseReductionMode::Anr:
        SetRXAANRVals(m_channel, 64, 16, 16e-4, 10e-7); // deskHPSDR's fixed taps/delay/gain/leakage
        SetRXAANRPosition(m_channel, 0);                // 0 = pre-AGC, deskHPSDR default
        SetRXAANRRun(m_channel, 1);
        break;
    case NoiseReductionMode::Emnr:
        SetRXAEMNRPosition(m_channel, 0);   // pre-AGC
        SetRXAEMNRgainMethod(m_channel, 2); // Gamma, deskHPSDR default
        SetRXAEMNRnpeMethod(m_channel, 0);  // OSMS, deskHPSDR default
        SetRXAEMNRaeRun(m_channel, 1);      // Artifact Elimination on, deskHPSDR default
        SetRXAEMNRpost2Run(m_channel, 0);   // post-processing off, deskHPSDR default
        SetRXAEMNRRun(m_channel, 1);
        break;
    case NoiseReductionMode::Off:
        break;
    }
}

void RxAudioChannel::setAutoNotch(bool enabled) {
    m_anfEnabled = enabled;
    if (!m_open) {
        return;
    }
    SetRXAANFPosition(m_channel, 0); // pre-AGC
    SetRXAANFRun(m_channel, enabled ? 1 : 0);
}

void RxAudioChannel::setSpectralNoiseBlanker(bool enabled) {
    m_snbEnabled = enabled;
    if (!m_open) {
        return;
    }
    SetRXASNBARun(m_channel, enabled ? 1 : 0);
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
    // Noise blanker runs in-place on the raw I/Q, before the demodulator -
    // matches core/deskhpsdr-src/receiver.c's rx_full_buffer() ordering.
    switch (m_nbMode) {
    case NoiseBlankerMode::Nb1:
        xanbEXT(m_channel, m_inBuffer.data(), m_inBuffer.data());
        break;
    case NoiseBlankerMode::Nb2:
        xnobEXT(m_channel, m_inBuffer.data(), m_inBuffer.data());
        break;
    case NoiseBlankerMode::Off:
        break;
    }

    int error = 0;
    fexchange0(m_channel, m_inBuffer.data(), m_outBuffer.data(), &error);

    QVector<float> out(int(m_outBuffer.size()));
    for (size_t i = 0; i < m_outBuffer.size(); ++i) {
        out[int(i)] = float(m_outBuffer[i]);
    }
    emit audioBlockReady(out);

    // 1 = RXA_S_AV (core/wdsp-2.00/RXA.h's `enum rxaMeterType`; not
    // included directly to avoid pulling WDSP's internal comm.h world
    // into this translation unit, same reasoning as the RxMode enum at
    // the top of rxaudio.h). Already in dBm - see
    // core/deskhpsdr-src/receiver.c's rx_get_smeter().
    constexpr int kRxaSAv = 1;
    emit meterUpdated(GetRXAMeter(m_channel, kRxaSAv));
}
