#ifndef QHPSDR_DEVICEPANEL_H
#define QHPSDR_DEVICEPANEL_H

#include <QWidget>

class QSlider;
class QLabel;
class QSpinBox;
class QPushButton;

// Shared front-end controls - settings that apply to the whole physical
// radio (one ADC, one Alex/filter-board connector), not to an individual
// receiver: RF gain calibration, the ADC0 step attenuator, Alex/Apollo
// filter-board auto-switching, and the wideband full-band sweep toggle.
// Split out of ToolbarWidget/VfoPanel when RX2 (a second, independently-
// tuned DDC sharing the same physical ADC0) was added - those settings
// only make sense once, shared between both receivers, not duplicated per
// receiver toolbar. Hosted as WidebandPanel's header strip: both are about
// the shared front-end rather than a specific receiver.
class DevicePanel : public QWidget {
    Q_OBJECT

public:
    explicit DevicePanel(QWidget *parent = nullptr);

    void setConnected(bool connected);

    // RF gain calibration offset in dB, subtracted from each receiver's
    // displayed S-meter reading (core/deskhpsdr-src/radio.c: adc[0].gain,
    // a pure calibration constant for standard - non-HermesLite2 - boards,
    // not sent to hardware).
    double rfGainDb() const;
    void setRfGainDb(double dB);

    // Standard HPSDR ADC0 step attenuator, 0-31 dB - added to each
    // receiver's raw dBFS meter reading before display (see
    // RadioConnection::setAttenuation()).
    int attenuationDb() const;
    void setAttenuationDb(int db);

    // Alex/Apollo-compatible filter board auto-switching - see
    // RadioConnection::setFilterBoardEnabled(). Off by default; only
    // meaningful over Protocol 2.
    bool filterBoardEnabled() const;
    void setFilterBoardEnabled(bool enabled);

    // Full-band ADC spectrum sweep - see RadioConnection::
    // setWidebandEnabled()'s doc comment (unverified wire format, only
    // meaningful over Protocol 2). Off by default.
    bool widebandEnabled() const;
    void setWidebandEnabled(bool enabled);

signals:
    void attenuationChanged(int db);
    void filterBoardEnabledChanged(bool enabled);
    void widebandEnabledChanged(bool enabled);

private:
    QSlider *m_rfGainSlider = nullptr;
    QLabel *m_rfGainValueLabel = nullptr;
    QSpinBox *m_attenSpin = nullptr;
    QPushButton *m_filterBoardButton = nullptr;
    QPushButton *m_widebandButton = nullptr;
};

#endif // QHPSDR_DEVICEPANEL_H
