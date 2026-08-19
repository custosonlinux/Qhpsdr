#include "toolbarwidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
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
        emit bandSelected(index, (b.lowHz + b.highHz) / 2.0);
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

    auto *fpsLabel = new QLabel(tr("FPS"), this);
    fpsLabel->setStyleSheet(kDarkLabelStyle);
    m_fpsCombo = new QComboBox(this);
    m_fpsCombo->setStyleSheet(kComboStyle);
    for (int f : {2, 5, 10, 15, 20, 30, 60}) {
        m_fpsCombo->addItem(tr("%1").arg(f), f);
    }
    m_fpsCombo->setCurrentIndex(4); // 20 fps, matches the previous fixed rate
    m_fpsCombo->setToolTip(tr("Panadapter/waterfall repaint rate. Lower this if the display feels "
                               "sluggish or is eating more CPU than you want - it doesn't affect audio."));
    connect(m_fpsCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) { emit fpsChanged(m_fpsCombo->itemData(index).toInt()); });

    auto *agcLabel = new QLabel(tr("AGC"), this);
    agcLabel->setStyleSheet(kDarkLabelStyle);
    m_agcCombo = new QComboBox(this);
    m_agcCombo->setStyleSheet(kComboStyle);
    // core/deskhpsdr-src/agc.h's _agc_enum + sliders.c's agc_labels[] -
    // no separate "auto" mode exists, this is the complete set.
    for (const char *label : {"AGC-OFF", "AGC-L", "AGC-S", "AGC-M", "AGC-F"}) {
        m_agcCombo->addItem(QString::fromLatin1(label));
    }
    m_agcCombo->setCurrentIndex(int(AgcMode::Medium));
    connect(m_agcCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) { emit agcModeChanged(AgcMode(index)); });

    m_agcTopSlider = new QSlider(Qt::Horizontal, this);
    m_agcTopSlider->setStyleSheet(kSliderStyle);
    // core/deskhpsdr-src/receiver.c: rx->agc_gain range -20..120dB, default 80.
    m_agcTopSlider->setRange(-20, 120);
    m_agcTopSlider->setValue(80);
    m_agcTopValueLabel = new QLabel(tr("80 dB"), this);
    m_agcTopValueLabel->setStyleSheet(kDarkLabelStyle);
    m_agcTopValueLabel->setMinimumWidth(48);
    m_agcTopSlider->setToolTip(tr("AGC gain ceiling (\"Top\") - applies regardless of AGC mode."));
    connect(m_agcTopSlider, &QSlider::valueChanged, this, [this](int value) {
        m_agcTopValueLabel->setText(tr("%1 dB").arg(value));
        emit agcTopChanged(double(value));
    });

    auto *nbLabel = new QLabel(tr("NB"), this);
    nbLabel->setStyleSheet(kDarkLabelStyle);
    m_nbCombo = new QComboBox(this);
    m_nbCombo->setStyleSheet(kComboStyle);
    for (const char *label : {"NB Off", "NB", "NB2"}) {
        m_nbCombo->addItem(QString::fromLatin1(label));
    }
    m_nbCombo->setToolTip(tr("Impulse noise blanker (two different algorithms - try both, NB2 often "
                              "does better on strong/close impulse noise)."));
    connect(m_nbCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) { emit noiseBlankerChanged(NoiseBlankerMode(index)); });

    auto *nrLabel = new QLabel(tr("NR"), this);
    nrLabel->setStyleSheet(kDarkLabelStyle);
    m_nrCombo = new QComboBox(this);
    m_nrCombo->setStyleSheet(kComboStyle);
    for (const char *label : {"NR Off", "ANR", "EMNR", "NR3", "NR4"}) {
        m_nrCombo->addItem(QString::fromLatin1(label));
    }
    m_nrCombo->setToolTip(tr("Noise reduction (broadband noise, not impulse noise - see NB for that). "
                              "EMNR is generally cleaner than ANR; NR3 (RNNoise) and NR4 (libspecbleach) "
                              "are newer AI/spectral algorithms worth comparing on real conditions."));
    connect(m_nrCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) { emit noiseReductionChanged(NoiseReductionMode(index)); });

    m_anfButton = new QPushButton(tr("ANF"), this);
    m_anfButton->setCheckable(true);
    m_anfButton->setStyleSheet(kToggleButtonStyle);
    m_anfButton->setToolTip(tr("Automatic notch filter - finds and removes steady heterodynes/carriers."));
    connect(m_anfButton, &QPushButton::toggled, this, &ToolbarWidget::autoNotchChanged);

    m_snbButton = new QPushButton(tr("SNB"), this);
    m_snbButton->setCheckable(true);
    m_snbButton->setStyleSheet(kToggleButtonStyle);
    m_snbButton->setToolTip(tr("Spectral noise blanker - a different algorithm from NB/NB2, sometimes "
                                "better on impulse/burst noise."));
    connect(m_snbButton, &QPushButton::toggled, this, &ToolbarWidget::spectralNoiseBlankerChanged);

    auto *nr4SmoothLabel = new QLabel(tr("NR4 Smooth"), this);
    nr4SmoothLabel->setStyleSheet(kDarkLabelStyle);
    m_nr4SmoothSlider = new QSlider(Qt::Horizontal, this);
    m_nr4SmoothSlider->setStyleSheet(kSliderStyle);
    m_nr4SmoothSlider->setRange(0, 100);
    m_nr4SmoothSlider->setValue(0);
    m_nr4SmoothValueLabel = new QLabel(tr("0%"), this);
    m_nr4SmoothValueLabel->setStyleSheet(kDarkLabelStyle);
    m_nr4SmoothValueLabel->setMinimumWidth(36);
    m_nr4SmoothSlider->setToolTip(
        tr("NR4's frame-to-frame smoothing. 0% (deskHPSDR's default) can sound choppy/jittery - "
           "raising it trades that for a more diffuse/smeared sound."));
    connect(m_nr4SmoothSlider, &QSlider::valueChanged, this, [this](int value) {
        m_nr4SmoothValueLabel->setText(tr("%1%").arg(value));
        emit nr4SmoothingChanged(double(value));
    });

    m_filterBoardButton = new QPushButton(tr("Filter Board"), this);
    m_filterBoardButton->setCheckable(true);
    m_filterBoardButton->setStyleSheet(kToggleButtonStyle);
    m_filterBoardButton->setToolTip(
        tr("Auto-switch an Alex/Apollo-compatible RX filter board by tuned frequency. Only take effect "
           "over Protocol 2 - Protocol 1 radios switch filters in firmware without this. Leave off unless "
           "a filter board is actually connected."));
    connect(m_filterBoardButton, &QPushButton::toggled, this, &ToolbarWidget::filterBoardEnabledChanged);

    m_widebandButton = new QPushButton(tr("Wideband"), this);
    m_widebandButton->setCheckable(true);
    m_widebandButton->setStyleSheet(kToggleButtonStyle);
    m_widebandButton->setToolTip(
        tr("Full-band ADC spectrum sweep instead of the tuned DDC's own zoomed view. Protocol 2 only - "
           "wire format is unverified against any working reference, needs real-hardware confirmation."));
    connect(m_widebandButton, &QPushButton::toggled, this, &ToolbarWidget::widebandEnabledChanged);

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
    layout->addSpacing(16);
    layout->addWidget(fpsLabel);
    layout->addWidget(m_fpsCombo);
    layout->addSpacing(16);
    layout->addWidget(agcLabel);
    layout->addWidget(m_agcCombo);
    layout->addWidget(m_agcTopSlider, /*stretch=*/1);
    layout->addWidget(m_agcTopValueLabel);
    layout->addSpacing(16);
    layout->addWidget(nbLabel);
    layout->addWidget(m_nbCombo);
    layout->addSpacing(16);
    layout->addWidget(nrLabel);
    layout->addWidget(m_nrCombo);
    layout->addWidget(m_anfButton);
    layout->addWidget(m_snbButton);
    layout->addSpacing(8);
    layout->addWidget(nr4SmoothLabel);
    layout->addWidget(m_nr4SmoothSlider, /*stretch=*/1);
    layout->addWidget(m_nr4SmoothValueLabel);
    layout->addSpacing(16);
    layout->addWidget(m_filterBoardButton);
    layout->addWidget(m_widebandButton);

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

int ToolbarWidget::fps() const { return m_fpsCombo ? m_fpsCombo->currentData().toInt() : 20; }

void ToolbarWidget::setFps(int fps) {
    if (!m_fpsCombo) {
        return;
    }
    for (int i = 0; i < m_fpsCombo->count(); ++i) {
        if (m_fpsCombo->itemData(i).toInt() == fps) {
            m_fpsCombo->setCurrentIndex(i);
            return;
        }
    }
}

int ToolbarWidget::bandIndexForFrequency(double hz) const {
    for (int i = 0; i < int(sizeof(kBands) / sizeof(kBands[0])); ++i) {
        if (hz >= kBands[i].lowHz && hz <= kBands[i].highHz) {
            return i;
        }
    }
    return -1;
}

int ToolbarWidget::bandCount() const { return int(sizeof(kBands) / sizeof(kBands[0])); }

double ToolbarWidget::bandCenterFrequencyHz(int index) const {
    if (index < 0 || index >= bandCount()) {
        return 0.0;
    }
    return (kBands[index].lowHz + kBands[index].highHz) / 2.0;
}

QString ToolbarWidget::currentBandLabel() const { return m_bandCombo ? m_bandCombo->currentText() : QString(); }

QString ToolbarWidget::currentNoiseBlankerLabel() const {
    return m_nbCombo ? m_nbCombo->currentText() : QString();
}

void ToolbarWidget::setFrequencyHz(double hz) {
    const int index = bandIndexForFrequency(hz);
    if (index >= 0) {
        const QSignalBlocker blocker(m_bandCombo);
        m_bandCombo->setCurrentIndex(index);
        return;
    }
}

void ToolbarWidget::setConnected(bool connected) {
    m_bandCombo->setEnabled(connected);
    m_afGainSlider->setEnabled(connected);
    m_rfGainSlider->setEnabled(connected);
    m_agcCombo->setEnabled(connected);
    m_agcTopSlider->setEnabled(connected);
    m_nbCombo->setEnabled(connected);
    m_nrCombo->setEnabled(connected);
    m_anfButton->setEnabled(connected);
    m_snbButton->setEnabled(connected);
    m_nr4SmoothSlider->setEnabled(connected);
    m_filterBoardButton->setEnabled(connected);
    m_widebandButton->setEnabled(connected);
}

AgcMode ToolbarWidget::agcMode() const { return m_agcCombo ? AgcMode(m_agcCombo->currentIndex()) : AgcMode::Medium; }

void ToolbarWidget::setAgcMode(AgcMode mode) {
    if (m_agcCombo) {
        m_agcCombo->setCurrentIndex(int(mode));
    }
}

double ToolbarWidget::agcTopDb() const { return m_agcTopSlider ? double(m_agcTopSlider->value()) : 80.0; }

void ToolbarWidget::setAgcTopDb(double dB) {
    if (m_agcTopSlider) {
        m_agcTopSlider->setValue(int(dB));
    }
}

NoiseBlankerMode ToolbarWidget::noiseBlankerMode() const {
    return m_nbCombo ? NoiseBlankerMode(m_nbCombo->currentIndex()) : NoiseBlankerMode::Off;
}

void ToolbarWidget::setNoiseBlankerMode(NoiseBlankerMode mode) {
    if (m_nbCombo) {
        m_nbCombo->setCurrentIndex(int(mode));
    }
}

NoiseReductionMode ToolbarWidget::noiseReductionMode() const {
    return m_nrCombo ? NoiseReductionMode(m_nrCombo->currentIndex()) : NoiseReductionMode::Off;
}

void ToolbarWidget::setNoiseReductionMode(NoiseReductionMode mode) {
    if (m_nrCombo) {
        m_nrCombo->setCurrentIndex(int(mode));
    }
}

bool ToolbarWidget::autoNotchEnabled() const { return m_anfButton && m_anfButton->isChecked(); }

void ToolbarWidget::setAutoNotchEnabled(bool enabled) {
    if (m_anfButton) {
        m_anfButton->setChecked(enabled);
    }
}

bool ToolbarWidget::spectralNoiseBlankerEnabled() const { return m_snbButton && m_snbButton->isChecked(); }

void ToolbarWidget::setSpectralNoiseBlankerEnabled(bool enabled) {
    if (m_snbButton) {
        m_snbButton->setChecked(enabled);
    }
}

double ToolbarWidget::nr4SmoothingFactor() const {
    return m_nr4SmoothSlider ? double(m_nr4SmoothSlider->value()) : 0.0;
}

void ToolbarWidget::setNr4SmoothingFactor(double percent) {
    if (m_nr4SmoothSlider) {
        m_nr4SmoothSlider->setValue(int(percent));
    }
}

bool ToolbarWidget::filterBoardEnabled() const {
    return m_filterBoardButton && m_filterBoardButton->isChecked();
}

void ToolbarWidget::setFilterBoardEnabled(bool enabled) {
    if (m_filterBoardButton) {
        m_filterBoardButton->setChecked(enabled);
    }
}

bool ToolbarWidget::widebandEnabled() const { return m_widebandButton && m_widebandButton->isChecked(); }

void ToolbarWidget::setWidebandEnabled(bool enabled) {
    if (m_widebandButton) {
        m_widebandButton->setChecked(enabled);
    }
}
