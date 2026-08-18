#include "toolbarwidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QSignalBlocker>
#include <QSlider>

namespace {

// Core HF (+6m) amateur bands, core/deskhpsdr-src/band.c's `bands[]` table
// (title, frequencyMin, frequencyMax in Hz) - VHF/UHF/XVTR entries omitted
// since a plain Hermes doesn't cover them.
struct BandEntry {
    const char *label;
    double lowHz;
    double highHz;
};
constexpr BandEntry kBands[] = {
    {"160m", 1800000.0, 2000000.0},   {"80m", 3500000.0, 4000000.0},
    {"60m", 5250000.0, 5450000.0},    {"40m", 7000000.0, 7300000.0},
    {"30m", 10100000.0, 10150000.0},  {"20m", 14000000.0, 14350000.0},
    {"17m", 18068000.0, 18168000.0},  {"15m", 21000000.0, 21450000.0},
    {"12m", 24890000.0, 24990000.0},  {"10m", 28000000.0, 29700000.0},
    {"6m", 50000000.0, 54000000.0},
};

const char *const kDarkLabelStyle = "color: #7fb6c4;";
const char *const kComboStyle =
    "QComboBox {"
    "  background: #1a222b;"
    "  color: #d8e6ea;"
    "  border: 1px solid #33424c;"
    "  border-radius: 3px;"
    "  padding: 2px 6px;"
    "}"
    "QComboBox:disabled { color: #5b6870; }"
    "QComboBox QAbstractItemView {"
    "  background: #1a222b;"
    "  color: #d8e6ea;"
    "  selection-background-color: #35526a;"
    "}";
const char *const kSliderStyle =
    "QSlider::groove:horizontal { background: #10161c; border: 1px solid #33424c; height: 4px; border-radius: 2px; }"
    "QSlider::handle:horizontal { background: #4a90a4; border: 1px solid #33424c; width: 12px; "
    "  margin: -5px 0; border-radius: 6px; }"
    "QSlider::handle:horizontal:disabled { background: #5b6870; }"
    "QSlider::groove:horizontal:disabled { background: #10161c; border-color: #262f37; }";

} // namespace

ToolbarWidget::ToolbarWidget(QWidget *parent) : QWidget(parent) {
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x0a, 0x0e, 0x13));
    setPalette(pal);

    auto *bandLabel = new QLabel(tr("Band"), this);
    bandLabel->setStyleSheet(kDarkLabelStyle);
    m_bandCombo = new QComboBox(this);
    m_bandCombo->setStyleSheet(kComboStyle);
    for (const auto &b : kBands) {
        m_bandCombo->addItem(QString::fromLatin1(b.label));
    }
    connect(m_bandCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (index < 0 || index >= int(sizeof(kBands) / sizeof(kBands[0]))) {
            return;
        }
        const auto &b = kBands[index];
        emit bandSelected((b.lowHz + b.highHz) / 2.0);
    });

    auto *afLabel = new QLabel(tr("AF Gain"), this);
    afLabel->setStyleSheet(kDarkLabelStyle);
    m_afGainSlider = new QSlider(Qt::Horizontal, this);
    m_afGainSlider->setStyleSheet(kSliderStyle);
    // core/deskhpsdr-src/sliders.c: af_gain_scale range -40..0 dB.
    m_afGainSlider->setRange(-40, 0);
    m_afGainSlider->setValue(0);
    m_afGainValueLabel = new QLabel(tr("0 dB"), this);
    m_afGainValueLabel->setStyleSheet(kDarkLabelStyle);
    m_afGainValueLabel->setMinimumWidth(48);
    connect(m_afGainSlider, &QSlider::valueChanged, this, [this](int value) {
        m_afGainValueLabel->setText(tr("%1 dB").arg(value));
        emit afGainChanged(double(value));
    });

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

    auto *zoomLabel = new QLabel(tr("Zoom"), this);
    zoomLabel->setStyleSheet(kDarkLabelStyle);
    m_zoomCombo = new QComboBox(this);
    m_zoomCombo->setStyleSheet(kComboStyle);
    for (int z : {1, 2, 4, 8, 16}) {
        m_zoomCombo->addItem(tr("%1x").arg(z), z);
    }
    m_zoomCombo->setToolTip(tr("Panadapter/waterfall zoom, centered on the tuned frequency."));

    auto *layout = new QHBoxLayout(this);
    layout->addWidget(bandLabel);
    layout->addWidget(m_bandCombo);
    layout->addSpacing(16);
    layout->addWidget(afLabel);
    layout->addWidget(m_afGainSlider, /*stretch=*/1);
    layout->addWidget(m_afGainValueLabel);
    layout->addSpacing(16);
    layout->addWidget(rfLabel);
    layout->addWidget(m_rfGainSlider, /*stretch=*/1);
    layout->addWidget(m_rfGainValueLabel);
    layout->addSpacing(16);
    layout->addWidget(zoomLabel);
    layout->addWidget(m_zoomCombo);

    setConnected(false);
}

double ToolbarWidget::rfGainDb() const { return m_rfGainSlider ? double(m_rfGainSlider->value()) : 0.0; }

void ToolbarWidget::setRfGainDb(double dB) {
    if (m_rfGainSlider) {
        m_rfGainSlider->setValue(int(dB));
    }
}

double ToolbarWidget::afGainDb() const { return m_afGainSlider ? double(m_afGainSlider->value()) : 0.0; }

void ToolbarWidget::setAfGainDb(double dB) {
    if (m_afGainSlider) {
        m_afGainSlider->setValue(int(dB));
    }
}

int ToolbarWidget::zoomFactor() const {
    return m_zoomCombo ? m_zoomCombo->currentData().toInt() : 1;
}

void ToolbarWidget::setZoomFactor(int factor) {
    if (!m_zoomCombo) {
        return;
    }
    for (int i = 0; i < m_zoomCombo->count(); ++i) {
        if (m_zoomCombo->itemData(i).toInt() == factor) {
            m_zoomCombo->setCurrentIndex(i);
            return;
        }
    }
}

void ToolbarWidget::setFrequencyHz(double hz) {
    for (int i = 0; i < int(sizeof(kBands) / sizeof(kBands[0])); ++i) {
        if (hz >= kBands[i].lowHz && hz <= kBands[i].highHz) {
            const QSignalBlocker blocker(m_bandCombo);
            m_bandCombo->setCurrentIndex(i);
            return;
        }
    }
}

void ToolbarWidget::setConnected(bool connected) {
    m_bandCombo->setEnabled(connected);
    m_afGainSlider->setEnabled(connected);
    m_rfGainSlider->setEnabled(connected);
}
