#ifndef QHPSDR_VFOPANEL_H
#define QHPSDR_VFOPANEL_H

#include <QWidget>

#include "rxaudio.h"

class QLineEdit;
class QComboBox;
class QProgressBar;
class QLabel;

// Minimal VFO panel: frequency readout/entry, mode + tuning-step selectors,
// and an (uncalibrated) signal level meter. Functionally modeled on
// deskHPSDR's VFO bar (core/deskhpsdr-src/vfo.c - mode list from mode.h,
// step table from vfo.c's `steps`/`step_labels`), but implemented as plain
// Qt widgets rather than the original's custom cairo-drawn LCD panel; this
// project's UI is being redesigned from scratch, not pixel-cloned.
class VfoPanel : public QWidget {
    Q_OBJECT

public:
    explicit VfoPanel(QWidget *parent = nullptr);

    // Sets displayed state without emitting the edited/selected signals -
    // for syncing the panel to state that changed elsewhere.
    void setFrequencyHz(double hz);
    double frequencyHz() const { return m_frequencyHz; }

    void setRxMode(RxMode mode);
    RxMode rxMode() const { return m_mode; }

    // level: 0.0-1.0, drives the (uncalibrated) signal meter.
    void setSignalLevel(double level);

    void setConnected(bool connected);

signals:
    // User-initiated changes (typed a frequency, picked a mode).
    void frequencyEditedHz(double hz);
    void modeSelected(RxMode mode);

protected:
    void wheelEvent(QWheelEvent *event) override;

private:
    void updateFrequencyDisplay();
    void onFrequencyEditingFinished();
    void onModeComboChanged(int index);

    QLineEdit *m_freqEdit = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_stepCombo = nullptr;
    QProgressBar *m_meter = nullptr;
    QLabel *m_meterLabel = nullptr;

    double m_frequencyHz = 7100000.0;
    RxMode m_mode = RxMode::AM;
};

#endif // QHPSDR_VFOPANEL_H
