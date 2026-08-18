#ifndef QHPSDR_TOOLBARWIDGET_H
#define QHPSDR_TOOLBARWIDGET_H

#include <QWidget>

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

    // RF gain calibration offset in dB, subtracted from the displayed
    // S-meter reading (core/deskhpsdr-src/radio.c: adc[0].gain, a pure
    // calibration constant for standard - non-HermesLite2 - boards, not
    // sent to hardware). Lets the user zero out the meter against a known
    // reference rather than trust the uncalibrated default.
    double rfGainDb() const;

signals:
    // Band picked from the combo - just a frequency to tune to (band
    // center), not a full band-state change (filters/antenna aren't
    // touched).
    void bandSelected(double frequencyHz);

    // AF gain in dB, -40..0 (core/deskhpsdr-src/sliders.c's af_gain_scale
    // range) - see RxAudioChannel::setAfGain().
    void afGainChanged(double dB);

private:
    QComboBox *m_bandCombo = nullptr;
    QSlider *m_afGainSlider = nullptr;
    QLabel *m_afGainValueLabel = nullptr;
    QSlider *m_rfGainSlider = nullptr;
    QLabel *m_rfGainValueLabel = nullptr;
};

#endif // QHPSDR_TOOLBARWIDGET_H
