#ifndef QHPSDR_VFOPANEL_H
#define QHPSDR_VFOPANEL_H

#include <QWidget>

#include "rxaudio.h"

class QLineEdit;
class QComboBox;
class QProgressBar;
class QLabel;
class QSpinBox;

// Minimal VFO panel: frequency readout/entry, mode + tuning-step + filter
// selectors, and an (uncalibrated) signal level meter. Functionally
// modeled on deskHPSDR's VFO bar (core/deskhpsdr-src/vfo.c - mode list
// from mode.h, step table from vfo.c's `steps`/`step_labels`, filter
// presets from core/filtertable.h), but implemented as plain Qt widgets
// rather than the original's custom cairo-drawn LCD panel; this project's
// UI is being redesigned from scratch, not pixel-cloned.
class VfoPanel : public QWidget {
    Q_OBJECT

public:
    explicit VfoPanel(QWidget *parent = nullptr);

    // Sets displayed state without emitting the edited/selected signals -
    // for syncing the panel to state that changed elsewhere.
    void setFrequencyHz(double hz);
    double frequencyHz() const { return m_frequencyHz; }

    // Also repopulates the filter combo for the new mode's preset list
    // and selects that mode's default filter (see core/filtertable.h).
    void setRxMode(RxMode mode);
    RxMode rxMode() const { return m_mode; }

    // WDSP's own S-meter reading in dBm (RxAudioChannel::meterUpdated()).
    // Converted to S-units the same way as deskHPSDR's meter.c: S9 =
    // -73dBm (HF convention; this project doesn't have a >30MHz-aware
    // path yet), 6dB/S-unit below S9, direct dB-over-S9 above it.
    void setSignalDbm(double dbm);

    // Current ADC0 step attenuator setting (0-31 dB) - added to the raw
    // dBFS meter reading before display so increasing attenuation doesn't
    // make the meter falsely show a weaker signal (see setSignalDbm()).
    int attenuationDb() const;

    void setConnected(bool connected);

signals:
    // User-initiated changes (typed a frequency, picked a mode/filter).
    void frequencyEditedHz(double hz);
    void modeSelected(RxMode mode);
    void filterSelected(double lowHz, double highHz);
    void attenuationChanged(int db);

protected:
    void wheelEvent(QWheelEvent *event) override;

private:
    void updateFrequencyDisplay();
    void onFrequencyEditingFinished();
    void onModeComboChanged(int index);
    void onFilterComboChanged(int index);
    void repopulateFilterCombo();

    QLineEdit *m_freqEdit = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_stepCombo = nullptr;
    QComboBox *m_filterCombo = nullptr;
    QSpinBox *m_attenSpin = nullptr;
    QProgressBar *m_meter = nullptr;
    QLabel *m_meterLabel = nullptr;

    double m_frequencyHz = 7100000.0;
    RxMode m_mode = RxMode::AM;
};

#endif // QHPSDR_VFOPANEL_H
