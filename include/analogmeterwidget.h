#ifndef QHPSDR_ANALOGMETERWIDGET_H
#define QHPSDR_ANALOGMETERWIDGET_H

#include <QWidget>

// Classic analog S-meter face (semicircular arc, needle, S1-S9+60dB scale) -
// styled after a real HF rig's meter (and deskHPSDR's own meter widget,
// see the reference screenshot this was modeled on): cream/ivory face,
// black ticks, red needle and over-S9 zone. Requested specifically for
// readability - a swept needle communicates signal strength/trend at a
// glance better than a numeric/bar readout, especially for the
// traditionally-trained ear/eye of an operator used to real meters.
class AnalogMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit AnalogMeterWidget(QWidget *parent = nullptr);

    // Raw signal level in dBm (WDSP's RXA_S_AV convention, same value
    // VfoPanel::setSignalDbm() already receives) - converted to an S-unit
    // needle position internally using s9Dbm().
    void setDbm(double dbm);

    // Calibration reference: the dBm value considered "S9" (HF convention
    // default -73dBm, matching VfoPanel's existing hardcoded constant -
    // see SettingsDialog's Meter tab). 6dB/S-unit below S9, direct
    // dB-over-S9 above it, same formula VfoPanel's digital readout uses.
    void setS9Dbm(double dbm);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize minimumSizeHint() const override { return QSize(220, 130); }

private:
    // Needle angle for the current dBm, 0.0 (full left, S0) to 1.0 (full
    // right, S9+60) - shared by paintEvent()'s needle draw and could be
    // reused for an animation later, hence factored out.
    double needleFraction() const;
    QString sMeterText() const;

    double m_dbm = -140.0;
    double m_s9Dbm = -73.0;
};

#endif // QHPSDR_ANALOGMETERWIDGET_H
