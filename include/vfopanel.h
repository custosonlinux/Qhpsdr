#ifndef QHPSDR_VFOPANEL_H
#define QHPSDR_VFOPANEL_H

#include <QWidget>

#include "filtertable.h"
#include "rxaudio.h"

class QLineEdit;
class QComboBox;
class QProgressBar;
class QLabel;
class QSpinBox;
class FrequencyLineEdit;

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

    // The currently-selected filter preset (name plus low/high offsets
    // from the tuned frequency) - for syncing things like the panadapter's
    // passband shading (PanadapterWidget::setPassband()) to whatever's
    // actually selected, not just to filterSelected()'s user-driven
    // signal (which doesn't fire for mode-change-driven default changes).
    FilterEntry currentFilter() const;

    // Filter/step combo index, for persisting and restoring the exact
    // selection (see MainWindow::saveSettings()/loadSettings()) rather
    // than just re-deriving each mode's default. Setting one doesn't
    // repopulate the other combo or change mode - callers restoring saved
    // state should set mode first, then these.
    int currentFilterIndex() const;
    void setFilterIndex(int index);
    int currentStepIndex() const;
    void setStepIndex(int index);
    void setAttenuationDb(int db);

    // WDSP's own S-meter reading in dBm (RxAudioChannel::meterUpdated()).
    // Converted to S-units the same way as deskHPSDR's meter.c: S9 =
    // -73dBm (HF convention; this project doesn't have a >30MHz-aware
    // path yet), 6dB/S-unit below S9, direct dB-over-S9 above it.
    void setSignalDbm(double dbm);

    // Current ADC0 step attenuator setting (0-31 dB) - added to the raw
    // dBFS meter reading before display so increasing attenuation doesn't
    // make the meter falsely show a weaker signal (see setSignalDbm()).
    int attenuationDb() const;

    // Band/noise-blanker labels for the summary line above the frequency
    // LCD (see updateInfoLabel()) - VfoPanel doesn't own either piece of
    // state itself (ToolbarWidget does), so MainWindow pushes them in
    // whenever they change. Empty band label omits that part of the
    // summary (e.g. a frequency outside all listed bands).
    void setBandLabel(const QString &label);
    void setNoiseBlankerLabel(const QString &label);

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
    friend class FrequencyLineEdit;

    void updateFrequencyDisplay();
    void onFrequencyEditingFinished();
    void onModeComboChanged(int index);
    void onFilterComboChanged(int index);
    void repopulateFilterCombo();
    // Digit-click tuning (FrequencyLineEdit, defined in vfopanel.cpp,
    // forwards mouse/key events here): clicking a digit in the frequency
    // readout highlights it and sets the Step combo to that digit's place
    // value; Up/Down then steps the frequency by whatever the Step combo
    // currently holds - the same single step value wheelEvent() already
    // uses, just two more ways to drive it.
    void onFreqDigitClicked(int charIndex);
    void stepFrequencyByDigit(int direction);
    void highlightSelectedDigit();
    // Recomposes m_infoLabel from band/mode/filter/noise-blanker whenever
    // any of them changes.
    void updateInfoLabel();

    QLabel *m_infoLabel = nullptr;
    QLineEdit *m_freqEdit = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_stepCombo = nullptr;
    QComboBox *m_filterCombo = nullptr;
    QSpinBox *m_attenSpin = nullptr;
    QProgressBar *m_meter = nullptr;
    QLabel *m_meterLabel = nullptr;

    double m_frequencyHz = 7100000.0;
    // Actual initial value is set in the constructor from
    // defaultModeForFrequency(m_frequencyHz) - see core/filtertable.h.
    RxMode m_mode = RxMode::LSB;
    // Power-of-ten place of the currently digit-click-selected/highlighted
    // digit (0 = units/1Hz, 3 = thousands/1kHz, ...) - matches
    // kDefaultStepIndex's 1kHz step until the user clicks a digit.
    int m_selectedDigitPlace = 3;
    QString m_bandLabel;
    QString m_noiseBlankerLabel = QStringLiteral("NB Off");
};

#endif // QHPSDR_VFOPANEL_H
