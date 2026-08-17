#include "rxaudio.h"

#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QStandardPaths>
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

    // 1 = RXA_S_AV (core/wdsp-2.00/RXA.h's `enum rxaMeterType`; not
    // included directly to avoid pulling WDSP's internal comm.h world
    // into this translation unit, same reasoning as the RxMode enum at
    // the top of rxaudio.h). Already in dBm - see
    // core/deskhpsdr-src/receiver.c's rx_get_smeter().
    constexpr int kRxaSAv = 1;
    emit meterUpdated(GetRXAMeter(m_channel, kRxaSAv));
}
