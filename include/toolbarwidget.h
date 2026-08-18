#ifndef QHPSDR_TOOLBARWIDGET_H
#define QHPSDR_TOOLBARWIDGET_H

#include <QWidget>

#include "rxaudio.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

// Band/gain toolbar, modeled on deskHPSDR's "sliders" bar
// (core/deskhpsdr-src/sliders.c) which sits below the receiver panel
// (panadapter+waterfall) in the real app's stacking order. Band selection
// is a plain combo, not deskHPSDR's popup-menu-with-multiple-stack-entries
// (core/deskhpsdr-src/band_menu.c/bandstack_menu.c - several stored
// frequency/mode slots per band, cycled by repeatedly clicking the same
// band) - a deliberate simplification down to a single "last used"
// band-stack entry per band, which MainWindow owns and persists (see
// MainWindow::retuneTo()/bandSelected's handler and save/loadSettings()).
// This widget only exposes what MainWindow needs for that: which band a
// frequency falls in (bandIndexForFrequency()) and each band's fallback
// center frequency (bandCenterFrequencyHz()) for bands with no saved entry
// yet.
class ToolbarWidget : public QWidget {
    Q_OBJECT

public:
    explicit ToolbarWidget(QWidget *parent = nullptr);

    void setConnected(bool connected);

    // Selects whichever band's range contains hz, without emitting
    // bandSelected() - for keeping the combo in sync with frequency
    // changes that didn't originate from picking a band (typed frequency,
    // wheel-tuning, panadapter click, or the app's own startup default).
    // No-op if hz doesn't fall in any listed band.
    void setFrequencyHz(double hz);

    // Index into the band list whose range contains hz, or -1 if none
    // does - for MainWindow's band-stack (see MainWindow::retuneTo()/the
    // bandSelected() handler), which needs to know *which* band a
    // frequency change happened in, not just a center frequency to fall
    // back to.
    int bandIndexForFrequency(double hz) const;

    // The band list's size, for band-stack persistence (MainWindow::save/
    // loadSettings()) - never changes after construction.
    int bandCount() const;

    // A band's center frequency by index, matching bandIndexForFrequency()'s
    // indexing - the same fallback bandSelected() itself uses when no
    // band-stack entry exists yet for that band. Returns 0 if out of range.
    double bandCenterFrequencyHz(int index) const;

    // RF gain calibration offset in dB, subtracted from the displayed
    // S-meter reading (core/deskhpsdr-src/radio.c: adc[0].gain, a pure
    // calibration constant for standard - non-HermesLite2 - boards, not
    // sent to hardware). Lets the user zero out the meter against a known
    // reference rather than trust the uncalibrated default.
    double rfGainDb() const;
    void setRfGainDb(double dB);

    // AF (output volume) gain in dB, -40..0 - see RxAudioChannel::setAfGain().
    double afGainDb() const;
    void setAfGainDb(double dB);

    // Panadapter/waterfall zoom: 1/2/4/8/16 - how much of the full
    // received span (the hardware's sample rate) is actually displayed,
    // centered on the tuned frequency. E.g. 4x on a 48kHz-wide receive
    // shows a 12kHz-wide slice, stretched across the same widget width -
    // see MainWindow::repaintDisplays(). Loosely modeled on deskHPSDR's
    // rx->zoom (core/deskhpsdr-src/receiver.c's rx_update_zoom()), but
    // without its separate manual-pan step: this always centers on the
    // current tuned frequency, panning implicitly as you retune.
    int zoomFactor() const;
    void setZoomFactor(int factor);

    // AGC mode + gain ("Top") - see RxAudioChannel::setAgcMode()/
    // setAgcTop() for the exact per-mode behavior this drives. No separate
    // "auto" mode exists (deskHPSDR doesn't have one either) - Off/Long/
    // Slow/Medium/Fast is the complete set.
    AgcMode agcMode() const;
    void setAgcMode(AgcMode mode);
    double agcTopDb() const;
    void setAgcTopDb(double dB);

    // Impulse noise blanker - see RxAudioChannel::setNoiseBlanker().
    NoiseBlankerMode noiseBlankerMode() const;
    void setNoiseBlankerMode(NoiseBlankerMode mode);

    // The other three "QRM fighters" - see RxAudioChannel::
    // setNoiseReduction()/setAutoNotch()/setSpectralNoiseBlanker().
    NoiseReductionMode noiseReductionMode() const;
    void setNoiseReductionMode(NoiseReductionMode mode);
    bool autoNotchEnabled() const;
    void setAutoNotchEnabled(bool enabled);
    bool spectralNoiseBlankerEnabled() const;
    void setSpectralNoiseBlankerEnabled(bool enabled);

    // NR4 (libspecbleach)'s frame-to-frame smoothing, 0-100% - see
    // RxAudioChannel::setNr4SmoothingFactor(). deskHPSDR defaults this to
    // 0 (no smoothing, can sound choppy/jittery); raising it trades that
    // for a more diffuse/smeared sound.
    double nr4SmoothingFactor() const;
    void setNr4SmoothingFactor(double percent);

signals:
    // Band picked from the combo, by index (matching
    // bandIndexForFrequency()) plus that band's center frequency - the
    // index lets MainWindow look up a saved band-stack entry (last
    // frequency/mode/filter used on that band) instead of always
    // retuning to center; centerFrequencyHz is what to fall back to when
    // no such entry exists yet.
    void bandSelected(int bandIndex, double centerFrequencyHz);

    // AF gain in dB, -40..0 (core/deskhpsdr-src/sliders.c's af_gain_scale
    // range) - see RxAudioChannel::setAfGain().
    void afGainChanged(double dB);

    void agcModeChanged(AgcMode mode);
    void agcTopChanged(double dB);
    void noiseBlankerChanged(NoiseBlankerMode mode);
    void noiseReductionChanged(NoiseReductionMode mode);
    void autoNotchChanged(bool enabled);
    void spectralNoiseBlankerChanged(bool enabled);
    void nr4SmoothingChanged(double percent);

private:
    QComboBox *m_bandCombo = nullptr;
    QSlider *m_afGainSlider = nullptr;
    QLabel *m_afGainValueLabel = nullptr;
    QSlider *m_rfGainSlider = nullptr;
    QLabel *m_rfGainValueLabel = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QComboBox *m_agcCombo = nullptr;
    QSlider *m_agcTopSlider = nullptr;
    QLabel *m_agcTopValueLabel = nullptr;
    QComboBox *m_nbCombo = nullptr;
    QComboBox *m_nrCombo = nullptr;
    QPushButton *m_anfButton = nullptr;
    QPushButton *m_snbButton = nullptr;
    QSlider *m_nr4SmoothSlider = nullptr;
    QLabel *m_nr4SmoothValueLabel = nullptr;
};

#endif // QHPSDR_TOOLBARWIDGET_H
