#ifndef QHPSDR_SETTINGSDIALOG_H
#define QHPSDR_SETTINGSDIALOG_H

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;

// Global/shared-settings window - deskHPSDR has ~30 menu categories
// (Radio/VFO/RX/TX/DSP/.../Extras, see the reference screenshots this was
// scoped from); most depend on features this port doesn't have yet (TX,
// PA, CW, MIDI, XVTR, CAT/TCI, Memory channels). This starts with the
// subset that's actually backed by something real right now, growing as
// more backend features land. Non-modal (created once by MainWindow,
// shown/raised rather than re-created) so it can stay open while the
// user watches the effect live on the panadapter/meter - explicitly
// requested ("ich muss das immer sehen um es beurteilen zu können").
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    // ADC0 dither/random-bit generators - see RadioConnection::
    // setAdcDither()'s doc comment. Shared across both receivers (one
    // physical ADC).
    bool adcDither() const;
    void setAdcDither(bool enabled);
    bool adcRandom() const;
    void setAdcRandom(bool enabled);

    // Panadapter peak-hold overlay - see PanadapterWidget::
    // setPeakHoldEnabled()'s doc comment. Applied to every panadapter
    // (RX1/RX2/Wideband) - a per-panel override isn't exposed yet.
    bool peakHoldEnabled() const;
    void setPeakHoldEnabled(bool enabled);
    double peakHoldTimeSec() const;
    void setPeakHoldTimeSec(double seconds);
    double peakHoldDropDbPerSec() const;
    void setPeakHoldDropDbPerSec(double dbPerSec);

    // Analog vs digital S-meter face - see VfoPanel::
    // setAnalogMeterEnabled()'s doc comment. Applied to both receivers.
    bool analogMeterEnabled() const;
    void setAnalogMeterEnabled(bool enabled);
    // dBm value considered "S9" - see VfoPanel::setS9Dbm()'s doc comment.
    double s9Dbm() const;
    void setS9Dbm(double dbm);

signals:
    void adcDitherChanged(bool enabled);
    void adcRandomChanged(bool enabled);
    // Bundled into one signal (not three) since PanadapterWidget's own
    // setPeakHoldEnabled()/setPeakHoldParams() are already split the same
    // way, and MainWindow only ever needs to push all three together.
    void peakHoldChanged(bool enabled, double holdTimeSec, double dropDbPerSec);
    void analogMeterChanged(bool enabled);
    void s9DbmChanged(double dbm);

private:
    QCheckBox *m_ditherCheck = nullptr;
    QCheckBox *m_randomCheck = nullptr;
    QCheckBox *m_peakHoldCheck = nullptr;
    QDoubleSpinBox *m_peakHoldTimeSpin = nullptr;
    QDoubleSpinBox *m_peakHoldDropSpin = nullptr;
    QCheckBox *m_analogMeterCheck = nullptr;
    QDoubleSpinBox *m_s9Spin = nullptr;
};

#endif // QHPSDR_SETTINGSDIALOG_H
