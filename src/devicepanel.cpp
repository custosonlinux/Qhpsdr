#include "devicepanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>

namespace {

const char *const kDarkLabelStyle = "color: #7fb6c4;";
const char *const kComboStyle =
    "QSpinBox {"
    "  background: #1a222b;"
    "  color: #d8e6ea;"
    "  border: 1px solid #33424c;"
    "  border-radius: 3px;"
    "  padding: 2px 6px;"
    "}"
    "QSpinBox:disabled { color: #5b6870; }";
const char *const kSliderStyle =
    "QSlider::groove:horizontal { background: #10161c; border: 1px solid #33424c; height: 4px; border-radius: 2px; }"
    "QSlider::handle:horizontal { background: #4a90a4; border: 1px solid #33424c; width: 12px; "
    "  margin: -5px 0; border-radius: 6px; }"
    "QSlider::handle:horizontal:disabled { background: #5b6870; }"
    "QSlider::groove:horizontal:disabled { background: #10161c; border-color: #262f37; }";
const char *const kToggleButtonStyle =
    "QPushButton {"
    "  background: #1a222b;"
    "  color: #d8e6ea;"
    "  border: 1px solid #33424c;"
    "  border-radius: 3px;"
    "  padding: 2px 8px;"
    "}"
    "QPushButton:disabled { color: #5b6870; }"
    "QPushButton:checked { background: #35526a; border-color: #4a90a4; color: #ffffff; }";

} // namespace

DevicePanel::DevicePanel(QWidget *parent) : QWidget(parent) {
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x0a, 0x0e, 0x13));
    setPalette(pal);

    auto *rfLabel = new QLabel(tr("RF Gain"), this);
    rfLabel->setStyleSheet(kDarkLabelStyle);
    m_rfGainSlider = new QSlider(Qt::Horizontal, this);
    m_rfGainSlider->setStyleSheet(kSliderStyle);
    m_rfGainSlider->setRange(-20, 20);
    m_rfGainSlider->setValue(0);
    m_rfGainValueLabel = new QLabel(tr("0 dB"), this);
    m_rfGainValueLabel->setStyleSheet(kDarkLabelStyle);
    m_rfGainValueLabel->setMinimumWidth(48);
    m_rfGainSlider->setToolTip(
        tr("S-meter calibration offset - not a hardware gain control (core/deskhpsdr-src/radio.c's "
           "adc[0].gain is a pure calibration constant for standard boards). Adjust until the meter "
           "matches a known reference signal."));
    connect(m_rfGainSlider, &QSlider::valueChanged, this, [this](int value) {
        m_rfGainValueLabel->setText(tr("%1 dB").arg(value));
    });

    auto *attenLabel = new QLabel(tr("Atten"), this);
    attenLabel->setStyleSheet(kDarkLabelStyle);
    m_attenSpin = new QSpinBox(this);
    m_attenSpin->setRange(0, 31);
    m_attenSpin->setSuffix(tr(" dB"));
    m_attenSpin->setStyleSheet(kComboStyle);
    m_attenSpin->setToolTip(
        tr("ADC0 step attenuator - raise this if the S-meter is pinned regardless of frequency "
           "(front-end/ADC overload). Shared by both receivers - one physical ADC."));
    connect(m_attenSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &DevicePanel::attenuationChanged);

    m_filterBoardButton = new QPushButton(tr("Filter Board"), this);
    m_filterBoardButton->setCheckable(true);
    m_filterBoardButton->setStyleSheet(kToggleButtonStyle);
    m_filterBoardButton->setToolTip(
        tr("Auto-switch an Alex/Apollo-compatible RX filter board by tuned frequency. Only takes effect "
           "over Protocol 2 - Protocol 1 radios switch filters in firmware without this. Leave off unless "
           "a filter board is actually connected."));
    connect(m_filterBoardButton, &QPushButton::toggled, this, &DevicePanel::filterBoardEnabledChanged);

    m_widebandButton = new QPushButton(tr("Wideband"), this);
    m_widebandButton->setCheckable(true);
    m_widebandButton->setStyleSheet(kToggleButtonStyle);
    m_widebandButton->setToolTip(
        tr("Full-band ADC spectrum sweep, shown in its own panadapter/waterfall panel. Protocol 2 only - "
           "wire format is unverified against any working reference, needs real-hardware confirmation."));
    connect(m_widebandButton, &QPushButton::toggled, this, &DevicePanel::widebandEnabledChanged);

    auto *layout = new QHBoxLayout(this);
    layout->addWidget(rfLabel);
    layout->addWidget(m_rfGainSlider, /*stretch=*/1);
    layout->addWidget(m_rfGainValueLabel);
    layout->addSpacing(16);
    layout->addWidget(attenLabel);
    layout->addWidget(m_attenSpin);
    layout->addSpacing(16);
    layout->addWidget(m_filterBoardButton);
    layout->addWidget(m_widebandButton);

    setConnected(false);
}

double DevicePanel::rfGainDb() const { return m_rfGainSlider ? double(m_rfGainSlider->value()) : 0.0; }

void DevicePanel::setRfGainDb(double dB) {
    if (m_rfGainSlider) {
        m_rfGainSlider->setValue(int(dB));
    }
}

int DevicePanel::attenuationDb() const { return m_attenSpin ? m_attenSpin->value() : 0; }

void DevicePanel::setAttenuationDb(int db) {
    if (m_attenSpin) {
        m_attenSpin->setValue(db);
    }
}

bool DevicePanel::filterBoardEnabled() const {
    return m_filterBoardButton && m_filterBoardButton->isChecked();
}

void DevicePanel::setFilterBoardEnabled(bool enabled) {
    if (m_filterBoardButton) {
        m_filterBoardButton->setChecked(enabled);
    }
}

bool DevicePanel::widebandEnabled() const { return m_widebandButton && m_widebandButton->isChecked(); }

void DevicePanel::setWidebandEnabled(bool enabled) {
    if (m_widebandButton) {
        m_widebandButton->setChecked(enabled);
    }
}

void DevicePanel::setConnected(bool connected) {
    m_rfGainSlider->setEnabled(connected);
    m_attenSpin->setEnabled(connected);
    m_filterBoardButton->setEnabled(connected);
    m_widebandButton->setEnabled(connected);
}
