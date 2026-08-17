#include "vfopanel.h"

#include "filtertable.h"

#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QIntValidator>
#include <QPalette>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

// core/deskhpsdr-src/vfo.c: steps[]/step_labels[].
constexpr qint64 kSteps[] = {1,     10,    25,    50,     100,    250,    500,    1000, 5000,
                             6250,  9000,  10000, 12500,  100000, 250000, 500000, 1000000};
const char *const kStepLabels[] = {"1Hz",  "10Hz", "25Hz",  "50Hz",  "100Hz", "250Hz", "500Hz", "1kHz", "5kHz",
                                    "6.25k", "9kHz", "10kHz", "12.5k", "100kHz", "250kHz", "500kHz", "1MHz"};
constexpr int kStepCount = int(sizeof(kSteps) / sizeof(kSteps[0]));
constexpr int kDefaultStepIndex = 7; // 1kHz

// core/deskhpsdr-src/mode.h / mode.c: _mode_enum / mode_string[].
struct ModeEntry {
    RxMode mode;
    const char *label;
};
constexpr ModeEntry kModes[] = {
    {RxMode::LSB, "LSB"}, {RxMode::USB, "USB"}, {RxMode::DSB, "DSB"}, {RxMode::CWL, "CWL"},
    {RxMode::CWU, "CWU"}, {RxMode::FM, "FMN"},  {RxMode::AM, "AM"},   {RxMode::DIGU, "DIGU"},
    {RxMode::SPEC, "SPEC"}, {RxMode::DIGL, "DIGL"}, {RxMode::SAM, "SAM"}, {RxMode::DRM, "DRM"},
};

} // namespace

namespace {
// Amber-on-black LCD look, styled after deskHPSDR's VFO "A:" frequency
// readout (core/deskhpsdr-src/vfo.c's cairo-drawn LCD panel).
const char *const kFreqEditStyle =
    "QLineEdit {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
    "    stop:0 #e08a1e, stop:1 #b6690f);"
    "  color: #14100a;"
    "  border: 1px solid #6b4108;"
    "  border-radius: 4px;"
    "  padding: 2px 10px;"
    "}"
    "QLineEdit:disabled {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
    "    stop:0 #4a4741, stop:1 #38352f);"
    "  color: #77716a;"
    "  border-color: #29271f;"
    "}";

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
} // namespace

VfoPanel::VfoPanel(QWidget *parent) : QWidget(parent) {
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x0a, 0x0e, 0x13));
    setPalette(pal);

    m_freqEdit = new QLineEdit(this);
    m_freqEdit->setValidator(new QIntValidator(0, 999999999, m_freqEdit));
    QFont freqFont("monospace");
    freqFont.setStyleHint(QFont::Monospace);
    freqFont.setPointSize(freqFont.pointSize() + 16);
    freqFont.setBold(true);
    m_freqEdit->setFont(freqFont);
    m_freqEdit->setAlignment(Qt::AlignRight);
    m_freqEdit->setStyleSheet(kFreqEditStyle);
    connect(m_freqEdit, &QLineEdit::editingFinished, this, &VfoPanel::onFrequencyEditingFinished);

    auto *hzLabel = new QLabel(tr("Hz"), this);
    hzLabel->setStyleSheet(kDarkLabelStyle);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->setStyleSheet(kComboStyle);
    for (const auto &entry : kModes) {
        m_modeCombo->addItem(QString::fromLatin1(entry.label));
    }
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &VfoPanel::onModeComboChanged);

    m_stepCombo = new QComboBox(this);
    m_stepCombo->setStyleSheet(kComboStyle);
    for (int i = 0; i < kStepCount; ++i) {
        m_stepCombo->addItem(QString::fromLatin1(kStepLabels[i]));
    }
    m_stepCombo->setCurrentIndex(kDefaultStepIndex);
    m_stepCombo->setToolTip(tr("Tuning step (scroll the frequency field to tune)"));

    m_filterCombo = new QComboBox(this);
    m_filterCombo->setStyleSheet(kComboStyle);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &VfoPanel::onFilterComboChanged);

    m_meterLabel = new QLabel(tr("Signal"), this);
    m_meterLabel->setStyleSheet(kDarkLabelStyle);
    m_meter = new QProgressBar(this);
    m_meter->setRange(0, 100);
    m_meter->setValue(0);
    m_meter->setTextVisible(false);
    m_meter->setStyleSheet(
        "QProgressBar { background: #10161c; border: 1px solid #33424c; border-radius: 3px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #2f8f3a, stop:0.7 #d8c22a, stop:1 #c23b2a); border-radius: 3px; }");
    m_meter->setToolTip(tr("Approximate signal level (uncalibrated - not yet an S-meter in dB)"));

    auto *modeLabel = new QLabel(tr("Mode"), this);
    modeLabel->setStyleSheet(kDarkLabelStyle);
    auto *stepLabel = new QLabel(tr("Step"), this);
    stepLabel->setStyleSheet(kDarkLabelStyle);
    auto *filterLabel = new QLabel(tr("Filter"), this);
    filterLabel->setStyleSheet(kDarkLabelStyle);

    auto *grid = new QGridLayout;
    grid->addWidget(m_freqEdit, 0, 0, 1, 3);
    grid->addWidget(hzLabel, 0, 3);
    grid->addWidget(modeLabel, 1, 0);
    grid->addWidget(m_modeCombo, 1, 1);
    grid->addWidget(stepLabel, 1, 2);
    grid->addWidget(m_stepCombo, 1, 3);
    grid->addWidget(filterLabel, 1, 4);
    grid->addWidget(m_filterCombo, 1, 5);
    grid->addWidget(m_meterLabel, 2, 0);
    grid->addWidget(m_meter, 2, 1, 1, 5);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(grid);
    layout->addStretch();

    updateFrequencyDisplay();
    setRxMode(m_mode);
    setConnected(false);
}

void VfoPanel::setFrequencyHz(double hz) {
    m_frequencyHz = hz;
    updateFrequencyDisplay();
}

void VfoPanel::updateFrequencyDisplay() {
    m_freqEdit->setText(QLocale::system().toString(qint64(m_frequencyHz)));
}

void VfoPanel::setRxMode(RxMode mode) {
    m_mode = mode;
    for (int i = 0; i < int(sizeof(kModes) / sizeof(kModes[0])); ++i) {
        if (kModes[i].mode == mode) {
            QSignalBlocker blocker(m_modeCombo);
            m_modeCombo->setCurrentIndex(i);
            break;
        }
    }
    repopulateFilterCombo();
}

void VfoPanel::repopulateFilterCombo() {
    QSignalBlocker blocker(m_filterCombo);
    m_filterCombo->clear();
    const auto filters = filtersForMode(m_mode);
    for (const auto &f : filters) {
        m_filterCombo->addItem(f.name);
    }
    const int defaultIndex = defaultFilterIndexForMode(m_mode);
    if (defaultIndex >= 0 && defaultIndex < filters.size()) {
        m_filterCombo->setCurrentIndex(defaultIndex);
    }
}

void VfoPanel::setSignalLevel(double level) {
    m_meter->setValue(int(qBound(0.0, level, 1.0) * 100));
}

void VfoPanel::setConnected(bool connected) {
    m_freqEdit->setEnabled(connected);
    m_modeCombo->setEnabled(connected);
    m_stepCombo->setEnabled(connected);
    m_filterCombo->setEnabled(connected);
}

void VfoPanel::onFrequencyEditingFinished() {
    bool ok = false;
    const double hz = QLocale::system().toLongLong(m_freqEdit->text(), &ok);
    if (!ok) {
        updateFrequencyDisplay();
        return;
    }
    m_frequencyHz = hz;
    emit frequencyEditedHz(m_frequencyHz);
}

void VfoPanel::onModeComboChanged(int index) {
    if (index < 0 || index >= int(sizeof(kModes) / sizeof(kModes[0]))) {
        return;
    }
    m_mode = kModes[index].mode;
    repopulateFilterCombo();
    emit modeSelected(m_mode);
}

void VfoPanel::onFilterComboChanged(int index) {
    const auto filters = filtersForMode(m_mode);
    if (index < 0 || index >= filters.size()) {
        return;
    }
    emit filterSelected(filters[index].low, filters[index].high);
}

void VfoPanel::wheelEvent(QWheelEvent *event) {
    if (!m_freqEdit->isEnabled()) {
        QWidget::wheelEvent(event);
        return;
    }
    const int stepIndex = qBound(0, m_stepCombo->currentIndex(), kStepCount - 1);
    const qint64 step = kSteps[stepIndex];
    const int ticks = event->angleDelta().y() / 120;
    if (ticks == 0) {
        return;
    }
    m_frequencyHz += double(ticks) * double(step);
    if (m_frequencyHz < 0) {
        m_frequencyHz = 0;
    }
    updateFrequencyDisplay();
    emit frequencyEditedHz(m_frequencyHz);
    event->accept();
}
