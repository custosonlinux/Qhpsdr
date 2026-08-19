#include "settingsdialog.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QPalette>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace {
const char *const kDarkLabelStyle = "color: #7fb6c4;";
const char *const kCheckStyle = "QCheckBox { color: #d8e6ea; }";
const char *const kSpinStyle =
    "QDoubleSpinBox {"
    "  background: #1a222b;"
    "  color: #d8e6ea;"
    "  border: 1px solid #33424c;"
    "  border-radius: 3px;"
    "  padding: 2px 6px;"
    "}";

QWidget *makeRadioTab(QCheckBox *&ditherCheck, QCheckBox *&randomCheck) {
    auto *tab = new QWidget;
    ditherCheck = new QCheckBox(QObject::tr("ADC0 Dither"), tab);
    ditherCheck->setStyleSheet(kCheckStyle);
    ditherCheck->setToolTip(
        QObject::tr("Adds a dither signal to the ADC to reduce quantization distortion at low signal "
                     "levels, at the cost of a slightly higher noise floor. Shared by both receivers - "
                     "one physical ADC0."));
    randomCheck = new QCheckBox(QObject::tr("ADC0 Random"), tab);
    randomCheck->setStyleSheet(kCheckStyle);
    randomCheck->setToolTip(
        QObject::tr("Adds a pseudo-random bit to further decorrelate quantization error. Standard ADC "
                     "linearization technique, same idea as Dither."));

    auto *layout = new QVBoxLayout(tab);
    layout->addWidget(ditherCheck);
    layout->addWidget(randomCheck);
    layout->addStretch();
    return tab;
}

QWidget *makeDisplayTab(QCheckBox *&peakHoldCheck, QDoubleSpinBox *&peakHoldTimeSpin,
                         QDoubleSpinBox *&peakHoldDropSpin) {
    auto *tab = new QWidget;
    peakHoldCheck = new QCheckBox(QObject::tr("Enable Peak Hold"), tab);
    peakHoldCheck->setStyleSheet(kCheckStyle);
    peakHoldCheck->setToolTip(
        QObject::tr("Shows a decaying max-hold line above the live panadapter trace, on every "
                     "receiver's panadapter (RX1/RX2/Wideband)."));

    auto *holdTimeLabel = new QLabel(QObject::tr("Decay hold time (s):"), tab);
    holdTimeLabel->setStyleSheet(kDarkLabelStyle);
    peakHoldTimeSpin = new QDoubleSpinBox(tab);
    peakHoldTimeSpin->setStyleSheet(kSpinStyle);
    peakHoldTimeSpin->setRange(0.0, 30.0);
    peakHoldTimeSpin->setSingleStep(0.5);
    peakHoldTimeSpin->setValue(2.5);
    peakHoldTimeSpin->setToolTip(QObject::tr("How long a peak stays put before it starts falling."));

    auto *dropLabel = new QLabel(QObject::tr("Drop (dB/s):"), tab);
    dropLabel->setStyleSheet(kDarkLabelStyle);
    peakHoldDropSpin = new QDoubleSpinBox(tab);
    peakHoldDropSpin->setStyleSheet(kSpinStyle);
    peakHoldDropSpin->setRange(0.1, 60.0);
    peakHoldDropSpin->setSingleStep(0.5);
    peakHoldDropSpin->setValue(6.0);
    peakHoldDropSpin->setToolTip(QObject::tr("How fast a peak falls once it starts decaying."));

    auto *grid = new QGridLayout;
    grid->addWidget(holdTimeLabel, 0, 0);
    grid->addWidget(peakHoldTimeSpin, 0, 1);
    grid->addWidget(dropLabel, 1, 0);
    grid->addWidget(peakHoldDropSpin, 1, 1);
    grid->setColumnStretch(2, 1);

    auto *layout = new QVBoxLayout(tab);
    layout->addWidget(peakHoldCheck);
    layout->addLayout(grid);
    layout->addStretch();
    return tab;
}

QWidget *makeMeterTab(QCheckBox *&analogMeterCheck, QDoubleSpinBox *&s9Spin) {
    auto *tab = new QWidget;
    analogMeterCheck = new QCheckBox(QObject::tr("Analog S-Meter"), tab);
    analogMeterCheck->setStyleSheet(kCheckStyle);
    analogMeterCheck->setToolTip(
        QObject::tr("Classic needle-gauge meter instead of the digital bar - applied to both receivers."));

    auto *s9Label = new QLabel(QObject::tr("S9 reference (dBm):"), tab);
    s9Label->setStyleSheet(kDarkLabelStyle);
    s9Spin = new QDoubleSpinBox(tab);
    s9Spin->setStyleSheet(kSpinStyle);
    s9Spin->setRange(-140.0, 0.0);
    s9Spin->setSingleStep(1.0);
    s9Spin->setValue(-73.0);
    s9Spin->setToolTip(
        QObject::tr("The dBm level the meter calls \"S9\" - HF convention default -73dBm "
                     "(core/deskhpsdr-src/meter.c). 6dB per S-unit below this, direct dB-over-S9 above it."));

    auto *grid = new QGridLayout;
    grid->addWidget(s9Label, 0, 0);
    grid->addWidget(s9Spin, 0, 1);
    grid->setColumnStretch(2, 1);

    auto *layout = new QVBoxLayout(tab);
    layout->addWidget(analogMeterCheck);
    layout->addLayout(grid);
    layout->addStretch();
    return tab;
}
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Qhpsdr - Settings"));
    setModal(false);
    resize(420, 320);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x0a, 0x0e, 0x13));
    setPalette(pal);
    setAutoFillBackground(true);

    auto *tabs = new QTabWidget(this);
    tabs->setStyleSheet("QTabWidget::pane { border: 1px solid #33424c; } "
                         "QTabBar::tab { background: #1a222b; color: #d8e6ea; padding: 6px 14px; } "
                         "QTabBar::tab:selected { background: #35526a; }");
    tabs->addTab(makeRadioTab(m_ditherCheck, m_randomCheck), tr("Radio"));
    tabs->addTab(makeDisplayTab(m_peakHoldCheck, m_peakHoldTimeSpin, m_peakHoldDropSpin), tr("Display"));
    tabs->addTab(makeMeterTab(m_analogMeterCheck, m_s9Spin), tr("Meter"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);

    connect(m_ditherCheck, &QCheckBox::toggled, this, &SettingsDialog::adcDitherChanged);
    connect(m_randomCheck, &QCheckBox::toggled, this, &SettingsDialog::adcRandomChanged);
    auto emitPeakHold = [this]() {
        emit peakHoldChanged(m_peakHoldCheck->isChecked(), m_peakHoldTimeSpin->value(), m_peakHoldDropSpin->value());
    };
    connect(m_peakHoldCheck, &QCheckBox::toggled, this, emitPeakHold);
    connect(m_peakHoldTimeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitPeakHold);
    connect(m_peakHoldDropSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, emitPeakHold);
    connect(m_analogMeterCheck, &QCheckBox::toggled, this, &SettingsDialog::analogMeterChanged);
    connect(m_s9Spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SettingsDialog::s9DbmChanged);
}

bool SettingsDialog::adcDither() const { return m_ditherCheck->isChecked(); }
void SettingsDialog::setAdcDither(bool enabled) { m_ditherCheck->setChecked(enabled); }
bool SettingsDialog::adcRandom() const { return m_randomCheck->isChecked(); }
void SettingsDialog::setAdcRandom(bool enabled) { m_randomCheck->setChecked(enabled); }

bool SettingsDialog::peakHoldEnabled() const { return m_peakHoldCheck->isChecked(); }
void SettingsDialog::setPeakHoldEnabled(bool enabled) { m_peakHoldCheck->setChecked(enabled); }
double SettingsDialog::peakHoldTimeSec() const { return m_peakHoldTimeSpin->value(); }
void SettingsDialog::setPeakHoldTimeSec(double seconds) { m_peakHoldTimeSpin->setValue(seconds); }
double SettingsDialog::peakHoldDropDbPerSec() const { return m_peakHoldDropSpin->value(); }
void SettingsDialog::setPeakHoldDropDbPerSec(double dbPerSec) { m_peakHoldDropSpin->setValue(dbPerSec); }

bool SettingsDialog::analogMeterEnabled() const { return m_analogMeterCheck->isChecked(); }
void SettingsDialog::setAnalogMeterEnabled(bool enabled) { m_analogMeterCheck->setChecked(enabled); }
double SettingsDialog::s9Dbm() const { return m_s9Spin->value(); }
void SettingsDialog::setS9Dbm(double dbm) { m_s9Spin->setValue(dbm); }
