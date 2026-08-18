#include "vfopanel.h"

#include "filtertable.h"

#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QIntValidator>
#include <QMouseEvent>
#include <QPalette>
#include <QProgressBar>
#include <QSpinBox>
#include <QStringList>
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

// kSteps index whose value is exactly 10^place, or -1 if there isn't one
// (there is, for every place from 0/1Hz to 6/1MHz - kSteps just also has
// some non-decade entries like 25/50/250/... in between).
int stepIndexForPlaceValue(int place) {
    qint64 target = 1;
    for (int i = 0; i < place; ++i) {
        target *= 10;
    }
    for (int i = 0; i < kStepCount; ++i) {
        if (kSteps[i] == target) {
            return i;
        }
    }
    return -1;
}

// Which power-of-ten place (0 = units, 3 = thousands, ...) the digit
// nearest character index chIdx belongs to, in a QLocale-grouped string
// like "7.120.000" - counts digits (skipping grouping separators) to the
// right of the nearest actual digit character. Returns -1 for empty text.
int placeValueForCharIndex(const QString &text, int chIdx) {
    if (text.isEmpty()) {
        return -1;
    }
    chIdx = qBound(0, chIdx, text.length() - 1);
    int idx = chIdx;
    while (idx < text.length() && !text.at(idx).isDigit()) {
        ++idx;
    }
    if (idx >= text.length()) {
        idx = chIdx;
        while (idx >= 0 && !text.at(idx).isDigit()) {
            --idx;
        }
    }
    if (idx < 0) {
        return -1;
    }
    int digitsToRight = 0;
    for (int j = idx + 1; j < text.length(); ++j) {
        if (text.at(j).isDigit()) {
            ++digitsToRight;
        }
    }
    return digitsToRight;
}

// Inverse of placeValueForCharIndex(): the character index of the digit
// whose place value is `place`, or -1 if text doesn't have that many
// digits.
int charIndexForPlaceValue(const QString &text, int place) {
    int digitsToRight = 0;
    for (int j = text.length() - 1; j >= 0; --j) {
        if (text.at(j).isDigit()) {
            if (digitsToRight == place) {
                return j;
            }
            ++digitsToRight;
        }
    }
    return -1;
}

} // namespace

// Forwards mouse clicks and Up/Down key presses to VfoPanel for
// digit-click tuning (see VfoPanel::onFreqDigitClicked()/
// stepFrequencyByDigit()) - defined here rather than in the header since
// it adds no new signals/slots (just overrides existing virtuals), so it
// doesn't need Q_OBJECT/moc processing.
class FrequencyLineEdit : public QLineEdit {
public:
    FrequencyLineEdit(VfoPanel *owner, QWidget *parent) : QLineEdit(parent), m_owner(owner) {}

protected:
    void mousePressEvent(QMouseEvent *event) override {
        const int charIndex = cursorPositionAt(event->pos());
        QLineEdit::mousePressEvent(event);
        m_owner->onFreqDigitClicked(charIndex);
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Up) {
            m_owner->stepFrequencyByDigit(+1);
            return;
        }
        if (event->key() == Qt::Key_Down) {
            m_owner->stepFrequencyByDigit(-1);
            return;
        }
        QLineEdit::keyPressEvent(event);
    }

private:
    VfoPanel *m_owner;
};

namespace {
// Amber-on-black LCD look, styled after deskHPSDR's VFO "A:" frequency
// readout (core/deskhpsdr-src/vfo.c's cairo-drawn LCD panel). The gradient/
// border live on the m_freqBox container (kFreqBoxStyle) rather than on
// m_freqEdit itself, since the info summary (m_infoLabel) shares the same
// box, left of the digits - both children stay transparent so the
// container's single amber field shows through both.
const char *const kFreqBoxStyle =
    "QWidget#freqBox {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
    "    stop:0 #e08a1e, stop:1 #b6690f);"
    "  border: 1px solid #6b4108;"
    "  border-radius: 4px;"
    "}"
    "QWidget#freqBox:disabled {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
    "    stop:0 #4a4741, stop:1 #38352f);"
    "  border-color: #29271f;"
    "}";

const char *const kFreqEditStyle =
    "QLineEdit {"
    "  background: transparent;"
    "  color: #14100a;"
    "  border: none;"
    "  padding: 2px 10px;"
    "  selection-background-color: #ffe680;"
    "  selection-color: #14100a;"
    "}"
    "QLineEdit:disabled { color: #77716a; }";

// Same amber-LCD family and color as the frequency digits (kFreqEditStyle)
// but smaller/plain, so it reads as "same field, same design" while the
// digits stay the focal point.
const char *const kInfoLabelStyle =
    "QLabel {"
    "  background: transparent;"
    "  color: #14100a;"
    "  border: none;"
    "  padding: 2px 0px 2px 10px;"
    "}"
    "QLabel:disabled { color: #77716a; }";

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

    QFont freqFont("monospace");
    freqFont.setStyleHint(QFont::Monospace);
    freqFont.setPointSize(freqFont.pointSize() + 16);
    freqFont.setBold(true);

    // A single amber LCD field: m_infoLabel (band/mode/filter/NB summary,
    // left-aligned) and m_freqEdit (the big digits, right-aligned) share
    // one box/gradient/border (kFreqBoxStyle) instead of two visually
    // separate strips - both children render transparent, same monospace
    // family as the digits, just smaller for the summary text.
    auto *freqBox = new QWidget(this);
    freqBox->setObjectName(QStringLiteral("freqBox"));
    freqBox->setStyleSheet(kFreqBoxStyle);

    m_infoLabel = new QLabel(freqBox);
    m_infoLabel->setStyleSheet(kInfoLabelStyle);
    QFont infoFont = freqFont;
    infoFont.setPointSizeF(freqFont.pointSizeF() * 0.45);
    infoFont.setLetterSpacing(QFont::PercentageSpacing, 105);
    m_infoLabel->setFont(infoFont);

    m_freqEdit = new FrequencyLineEdit(this, freqBox);
    m_freqEdit->setValidator(new QIntValidator(0, 999999999, m_freqEdit));
    m_freqEdit->setFont(freqFont);
    m_freqEdit->setAlignment(Qt::AlignRight);
    m_freqEdit->setStyleSheet(kFreqEditStyle);
    connect(m_freqEdit, &QLineEdit::editingFinished, this, &VfoPanel::onFrequencyEditingFinished);

    auto *freqBoxLayout = new QHBoxLayout(freqBox);
    freqBoxLayout->setContentsMargins(0, 0, 0, 0);
    freqBoxLayout->setSpacing(0);
    freqBoxLayout->addWidget(m_infoLabel, 0, Qt::AlignVCenter | Qt::AlignLeft);
    freqBoxLayout->addStretch();
    freqBoxLayout->addWidget(m_freqEdit, 0, Qt::AlignVCenter | Qt::AlignRight);

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

    m_attenSpin = new QSpinBox(this);
    m_attenSpin->setRange(0, 31);
    m_attenSpin->setSuffix(tr(" dB"));
    m_attenSpin->setStyleSheet(kComboStyle);
    m_attenSpin->setToolTip(
        tr("ADC0 step attenuator - raise this if the S-meter is pinned regardless of frequency "
           "(front-end/ADC overload)."));
    connect(m_attenSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &VfoPanel::attenuationChanged);

    m_meterLabel = new QLabel(tr("Signal"), this);
    m_meterLabel->setStyleSheet(kDarkLabelStyle);
    m_meter = new QProgressBar(this);
    // Range in tenths of a dB, from S0 (-127 dBm) to S9+60 (-13 dBm) - see
    // setSignalDbm(). QProgressBar only does integer steps, hence tenths
    // rather than dBm directly, for smoother-looking movement.
    m_meter->setRange(-1270, -130);
    m_meter->setValue(-1270);
    m_meter->setTextVisible(true);
    m_meter->setFormat(tr("no signal"));
    m_meter->setAlignment(Qt::AlignCenter);
    m_meter->setStyleSheet(
        "QProgressBar { background: #10161c; border: 1px solid #33424c; border-radius: 3px; color: #d8e6ea; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #2f8f3a, stop:0.7 #d8c22a, stop:1 #c23b2a); border-radius: 3px; }");
    m_meter->setToolTip(tr("S-meter (WDSP's RXA_S_AV, HF S9 = -73dBm convention)"));

    auto *modeLabel = new QLabel(tr("Mode"), this);
    modeLabel->setStyleSheet(kDarkLabelStyle);
    auto *stepLabel = new QLabel(tr("Step"), this);
    stepLabel->setStyleSheet(kDarkLabelStyle);
    auto *filterLabel = new QLabel(tr("Filter"), this);
    filterLabel->setStyleSheet(kDarkLabelStyle);
    auto *attenLabel = new QLabel(tr("Atten"), this);
    attenLabel->setStyleSheet(kDarkLabelStyle);

    auto *grid = new QGridLayout;
    grid->setVerticalSpacing(0);
    grid->addWidget(freqBox, 0, 0, 1, 3);
    grid->addWidget(hzLabel, 0, 3);
    grid->setRowMinimumHeight(1, 6); // spacing below the LCD field
    grid->addWidget(modeLabel, 3, 0);
    grid->addWidget(m_modeCombo, 3, 1);
    grid->addWidget(stepLabel, 3, 2);
    grid->addWidget(m_stepCombo, 3, 3);
    grid->addWidget(filterLabel, 3, 4);
    grid->addWidget(m_filterCombo, 3, 5);
    grid->addWidget(attenLabel, 3, 6);
    grid->addWidget(m_attenSpin, 3, 7);
    grid->addWidget(m_meterLabel, 4, 0);
    grid->addWidget(m_meter, 4, 1, 1, 7);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(grid);
    layout->addStretch();

    updateFrequencyDisplay();
    setRxMode(defaultModeForFrequency(m_frequencyHz));
    setConnected(false);
}

void VfoPanel::setFrequencyHz(double hz) {
    m_frequencyHz = hz;
    updateFrequencyDisplay();
}

void VfoPanel::updateFrequencyDisplay() {
    m_freqEdit->setText(QLocale::system().toString(qint64(m_frequencyHz)));
    highlightSelectedDigit();
}

void VfoPanel::onFreqDigitClicked(int charIndex) {
    const int place = placeValueForCharIndex(m_freqEdit->text(), charIndex);
    if (place < 0) {
        return;
    }
    m_selectedDigitPlace = place;
    const int stepIdx = stepIndexForPlaceValue(place);
    if (stepIdx >= 0) {
        m_stepCombo->setCurrentIndex(stepIdx);
    }
    highlightSelectedDigit();
}

void VfoPanel::stepFrequencyByDigit(int direction) {
    const int stepIdx = qBound(0, m_stepCombo->currentIndex(), kStepCount - 1);
    const qint64 step = kSteps[stepIdx];
    m_frequencyHz += double(direction) * double(step);
    if (m_frequencyHz < 0) {
        m_frequencyHz = 0;
    }
    updateFrequencyDisplay();
    emit frequencyEditedHz(m_frequencyHz);
}

void VfoPanel::highlightSelectedDigit() {
    if (!m_freqEdit->hasFocus()) {
        return;
    }
    const int idx = charIndexForPlaceValue(m_freqEdit->text(), m_selectedDigitPlace);
    if (idx >= 0) {
        m_freqEdit->setSelection(idx, 1);
    }
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
    updateInfoLabel();
}

int VfoPanel::attenuationDb() const { return m_attenSpin ? m_attenSpin->value() : 0; }

FilterEntry VfoPanel::currentFilter() const {
    const auto filters = filtersForMode(m_mode);
    const int idx = m_filterCombo->currentIndex();
    if (idx >= 0 && idx < filters.size()) {
        return filters[idx];
    }
    return FilterEntry{};
}

int VfoPanel::currentFilterIndex() const { return m_filterCombo->currentIndex(); }

void VfoPanel::setFilterIndex(int index) {
    if (index >= 0 && index < m_filterCombo->count()) {
        m_filterCombo->setCurrentIndex(index);
    }
    updateInfoLabel();
}

int VfoPanel::currentStepIndex() const { return m_stepCombo->currentIndex(); }

void VfoPanel::setStepIndex(int index) {
    if (index >= 0 && index < m_stepCombo->count()) {
        m_stepCombo->setCurrentIndex(index);
    }
}

void VfoPanel::setAttenuationDb(int db) { m_attenSpin->setValue(db); }

void VfoPanel::setSignalDbm(double dbm) {
    // core/deskhpsdr-src/meter.c: S9 = -73dBm (HF), 6dB per S-unit below
    // S9, direct dB-over-S9 above it (e.g. "S9+20").
    constexpr double kS9Dbm = -73.0;
    QString text;
    if (dbm <= kS9Dbm) {
        const double sUnits = qMax(0.0, 9.0 + (dbm - kS9Dbm) / 6.0);
        text = QStringLiteral("S%1").arg(int(sUnits));
    } else {
        text = QStringLiteral("S9+%1").arg(int(dbm - kS9Dbm));
    }
    m_meter->setFormat(text + tr("  (%1 dBm)").arg(int(dbm)));
    m_meter->setValue(int(qBound(-1270.0, dbm * 10.0, -130.0)));
}

void VfoPanel::setBandLabel(const QString &label) {
    m_bandLabel = label;
    updateInfoLabel();
}

void VfoPanel::setNoiseBlankerLabel(const QString &label) {
    m_noiseBlankerLabel = label;
    updateInfoLabel();
}

void VfoPanel::updateInfoLabel() {
    QStringList parts;
    if (!m_bandLabel.isEmpty()) {
        parts << m_bandLabel;
    }
    parts << m_modeCombo->currentText();
    if (m_filterCombo->count() > 0) {
        parts << m_filterCombo->currentText();
    }
    if (!m_noiseBlankerLabel.isEmpty()) {
        parts << m_noiseBlankerLabel;
    }
    m_infoLabel->setText(parts.join(QStringLiteral("   •   ")));
}

void VfoPanel::setConnected(bool connected) {
    // m_freqEdit's parent is freqBox (the shared amber LCD field) - disable
    // it too so kFreqBoxStyle's QWidget#freqBox:disabled rule (grey
    // gradient) applies, not just the QLineEdit's own disabled text color.
    m_freqEdit->parentWidget()->setEnabled(connected);
    m_freqEdit->setEnabled(connected);
    m_modeCombo->setEnabled(connected);
    m_stepCombo->setEnabled(connected);
    m_filterCombo->setEnabled(connected);
    m_attenSpin->setEnabled(connected);
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
    updateInfoLabel();
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
