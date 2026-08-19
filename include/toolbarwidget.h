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

    // Current band-combo/noise-blanker-combo text, for VfoPanel's summary
    // line above the frequency LCD (MainWindow pushes these into
    // VfoPanel::setBandLabel()/setNoiseBlankerLabel() whenever they
    // change) - just the combo's own displayed text, no separate state.
    QString currentBandLabel() const;
    QString currentNoiseBlankerLabel() const;

    // AF (output volume) gain in dB, -40..0 - see RxAudioChannel::setAfGain().
    double afGainDb() const;
    void setAfGainDb(double dB);

    // This receiver's own DDC sample rate in Hz - 48000/96000/192000/
    // 384000/768000/1536000 (matches deskHPSDR's supported Hermes rates).
    // Independent per receiver: each DDC has its own decimator in the
    // FPGA, so RX1/RX2 can legitimately run at different rates
    // simultaneously off the same physical ADC - see RadioConnection::
    // setRxSampleRate()/setRxSampleRate2(). Changing this requires
    // reopening the RxAudioChannel (WDSP bakes the input rate in at
    // OpenChannel() time) - see sampleRateChanged()/ReceiverPanel.
    int sampleRateHz() const;
    void setSampleRateHz(int hz);

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

    // Panadapter/waterfall repaint rate - see MainWindow's m_repaintTimer.
    // Decoupled from the ~23/s rate spectrum frames actually arrive at;
    // lower settings trade update smoothness for less GUI-thread paint
    // cost (useful on slower machines or very large window sizes).
    int fps() const;
    void setFps(int fps);

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

    // Panadapter/waterfall repaint rate in frames per second - see fps().
    void fpsChanged(int fps);

    // AF gain in dB, -40..0 (core/deskhpsdr-src/sliders.c's af_gain_scale
    // range) - see RxAudioChannel::setAfGain().
    void afGainChanged(double dB);

    // This receiver's DDC sample rate in Hz - see sampleRateHz().
    void sampleRateChanged(int hz);

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
    QComboBox *m_zoomCombo = nullptr;
    QComboBox *m_fpsCombo = nullptr;
    QComboBox *m_rateCombo = nullptr;
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
