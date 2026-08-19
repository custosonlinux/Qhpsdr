#ifndef QHPSDR_WIDEBANDPANEL_H
#define QHPSDR_WIDEBANDPANEL_H

#include <QVector>
#include <QWidget>

class PanadapterWidget;
class WaterfallWidget;
class DevicePanel;

// Full-band ADC spectrum sweep, shown in its own dedicated panadapter/
// waterfall pair - separate from both ReceiverPanel instances' own
// (narrow, DDC-tuned) panadapter/waterfall widgets, so it can be an
// independently visible/floatable/tabbable dock (see MainWindow) instead
// of the mode-switch-on-borrowed-widgets hack this replaced. Hosts
// DevicePanel as its header strip: RF Gain/Attenuation/Filter-Board/
// Wideband are all shared-front-end settings, not specific to either
// receiver, so pairing them here is natural. Fed exclusively by
// RadioConnection::wideSpectrumReady - see setSpectrum().
class WidebandPanel : public QWidget {
    Q_OBJECT

public:
    explicit WidebandPanel(QWidget *parent = nullptr);

    // Owned by this widget - MainWindow wires DevicePanel's signals to the
    // shared RadioConnection and to both ReceiverPanel's meter compensation
    // (see ReceiverPanel::setDevicePanel()).
    DevicePanel *devicePanel() const { return m_devicePanel; }
    // For SettingsDialog's Display tab (peak-hold applies to every
    // panadapter, not just the two receivers').
    PanadapterWidget *panadapter() const { return m_panadapter; }

    // One full sweep - see RadioConnection::wideSpectrumReady's doc
    // comment (one-sided 0Hz..sampleRateHz, not centered like a DDC
    // panadapter's complex-baseband view).
    void setSpectrum(const QVector<float> &magnitudesDb, double sampleRateHz);

private:
    DevicePanel *m_devicePanel = nullptr;
    PanadapterWidget *m_panadapter = nullptr;
    WaterfallWidget *m_waterfall = nullptr;
};

#endif // QHPSDR_WIDEBANDPANEL_H
