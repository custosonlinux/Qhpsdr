#ifndef QHPSDR_TOOLBARWIDGET_H
#define QHPSDR_TOOLBARWIDGET_H

#include <QWidget>

#include "rxaudio.h"

class QComboBox;
class QLabel;
class QSlider;

// Band/gain toolbar, modeled on deskHPSDR's "sliders" bar
// (core/deskhpsdr-src/sliders.c) which sits below the receiver panel
// (panadapter+waterfall) in the real app's stacking order. deskHPSDR's
// actual band control is a single "Band" button opening a popup menu
// (band_menu.c) that recalls each band's last-tuned frequency via its
// bandstack - not implemented here; picking a band here just retunes to
// that band's center frequency, a deliberate simplification.
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

signals:
    // Band picked from the combo - just a frequency to tune to (band
    // center), not a full band-state change (filters/antenna aren't
    // touched).
    void bandSelected(double frequencyHz);

    // AF gain in dB, -40..0 (core/deskhpsdr-src/sliders.c's af_gain_scale
    // range) - see RxAudioChannel::setAfGain().
    void afGainChanged(double dB);

    void agcModeChanged(AgcMode mode);
    void agcTopChanged(double dB);
    void noiseBlankerChanged(NoiseBlankerMode mode);

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
};

#endif // QHPSDR_TOOLBARWIDGET_H
