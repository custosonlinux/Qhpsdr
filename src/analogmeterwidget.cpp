#include "analogmeterwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {
// Sweep geometry: a 120-degree fan, needle pointing straight up at center.
// Matches a classic analog S-meter's proportions better than a full
// semicircle (which reads as too "flat" for a small widget).
constexpr double kSweepDeg = 120.0;
constexpr double kHalfSweepDeg = kSweepDeg / 2.0;

// Two-segment scale, same idea as a real meter: S0..S9 gets more angular
// room than the compressed S9+20/+40/+60 extension, since S-units are the
// range operators actually read precisely, dB-over-S9 is just "how far
// into the red".
constexpr double kSplitFraction = 0.62;

// deskHPSDR's own S-meter convention (core/deskhpsdr-src/meter.c, and
// VfoPanel::setSignalDbm() already uses the same numbers): S9 = -73dBm,
// 6dB/S-unit below S9. Sweep floor is S0.
constexpr double kDbmPerSUnit = 6.0;
constexpr double kOverS9RangeDb = 60.0;

// Point on the arc at radius r, angle a measured in degrees from vertical
// (0 = straight up, positive = clockwise/right) - screen coordinates
// (y grows downward), pivot at the origin passed by the caller.
QPointF arcPoint(const QPointF &pivot, double radius, double angleDeg) {
    const double rad = qDegreesToRadians(angleDeg);
    return {pivot.x() + radius * std::sin(rad), pivot.y() - radius * std::cos(rad)};
}
} // namespace

AnalogMeterWidget::AnalogMeterWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(minimumSizeHint());
}

void AnalogMeterWidget::setDbm(double dbm) {
    if (qFuzzyCompare(m_dbm + 1.0, dbm + 1.0)) {
        return;
    }
    m_dbm = dbm;
    update();
}

void AnalogMeterWidget::setS9Dbm(double dbm) {
    if (qFuzzyCompare(m_s9Dbm + 1.0, dbm + 1.0)) {
        return;
    }
    m_s9Dbm = dbm;
    update();
}

double AnalogMeterWidget::needleFraction() const {
    const double s0Dbm = m_s9Dbm - 9.0 * kDbmPerSUnit;
    if (m_dbm <= m_s9Dbm) {
        const double f = (m_dbm - s0Dbm) / (m_s9Dbm - s0Dbm);
        return qBound(0.0, f * kSplitFraction, kSplitFraction);
    }
    const double over = qBound(0.0, m_dbm - m_s9Dbm, kOverS9RangeDb);
    return kSplitFraction + (over / kOverS9RangeDb) * (1.0 - kSplitFraction);
}

QString AnalogMeterWidget::sMeterText() const {
    if (m_dbm <= m_s9Dbm) {
        const double sUnits = qMax(0.0, 9.0 + (m_dbm - m_s9Dbm) / kDbmPerSUnit);
        return QStringLiteral("S%1").arg(int(sUnits));
    }
    return QStringLiteral("S9+%1").arg(int(m_dbm - m_s9Dbm));
}

void AnalogMeterWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF full = rect();
    // Bezel - matches the app's dark widget chrome (kDarkLabelStyle
    // family's #1a222b/#33424c tokens used throughout the other panels).
    p.setPen(QPen(QColor(0x33, 0x42, 0x4c), 1));
    p.setBrush(QColor(0x1a, 0x22, 0x2b));
    p.drawRoundedRect(full.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);

    // Meter face: cream/ivory, like a real analog meter's paper dial -
    // deliberately different from the app's own dark palette, since the
    // point is to look like a physical instrument mounted in the panel,
    // not another flat digital widget.
    // Margin based on the SMALLER dimension, and radius clamped to a
    // sane minimum: a widget placed somewhere much wider than it is tall
    // (as happened when this lived inside VfoPanel's meter row) previously
    // computed a margin from width alone, which could make
    // `height - margin*2.5` go negative - a negative radius flips every
    // arcPoint() through the pivot, rendering the whole gauge rotated
    // 180° (needle hanging down instead of swinging up). Guarding here
    // keeps the geometry sane regardless of how cramped the host is.
    const double margin = qMin(full.width(), full.height()) * 0.06;
    const QPointF pivot(full.center().x(), full.bottom() - margin - full.height() * 0.05);
    const double radius = qMax(4.0, qMin(full.width() / 2.0 - margin, full.height() - margin * 2.5));

    QPainterPath facePath;
    const QPointF left = arcPoint(pivot, radius * 1.14, -kHalfSweepDeg);
    const QPointF right = arcPoint(pivot, radius * 1.14, kHalfSweepDeg);
    facePath.moveTo(pivot);
    facePath.lineTo(left);
    QRectF faceArcRect(pivot.x() - radius * 1.14, pivot.y() - radius * 1.14, radius * 1.14 * 2, radius * 1.14 * 2);
    facePath.arcTo(faceArcRect, 90.0 + kHalfSweepDeg, -kSweepDeg);
    facePath.lineTo(pivot);
    facePath.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xe8, 0xe1, 0xcd));
    p.drawPath(facePath);

    // Colored zone arc just inside the rim: green/amber for S1-S9, red for
    // the S9+20/40/60 extension - mirrors the reference deskHPSDR meter's
    // red over-S9 arc.
    const double zoneRadius = radius * 1.02;
    QPen zonePen;
    zonePen.setWidth(qMax(2.0, full.height() * 0.05));
    zonePen.setCapStyle(Qt::FlatCap);
    QRectF zoneRect(pivot.x() - zoneRadius, pivot.y() - zoneRadius, zoneRadius * 2, zoneRadius * 2);
    // Qt's drawArc() angle convention (0 = east/3-o'clock, positive =
    // counterclockwise) is the OPPOSITE rotation sense from arcPoint()'s
    // "0 = up, positive = clockwise" convention used everywhere else in
    // this file (ticks, needle) - convert explicitly rather than mixing
    // the two, which previously drew the green/red zones mirrored (red on
    // the low-signal side instead of the high one).
    auto toQtAngle16 = [](double myAngleDeg) { return int(qRound((90.0 - myAngleDeg) * 16.0)); };
    const int zoneStartAngle16 = toQtAngle16(-kHalfSweepDeg);
    const int splitAngle16 = toQtAngle16(-kHalfSweepDeg + kSweepDeg * kSplitFraction);
    const int zoneEndAngle16 = toQtAngle16(kHalfSweepDeg);
    zonePen.setColor(QColor(0x3a, 0x8f, 0x4a));
    p.setPen(zonePen);
    p.drawArc(zoneRect, zoneStartAngle16, splitAngle16 - zoneStartAngle16);
    zonePen.setColor(QColor(0xc2, 0x3b, 0x2a));
    p.setPen(zonePen);
    p.drawArc(zoneRect, splitAngle16, zoneEndAngle16 - splitAngle16);

    // Ticks + labels: S1/3/5/7/9 in the S-unit segment, +20/+40/+60 in the
    // over-S9 segment - same label set as the reference screenshot.
    p.setPen(QPen(QColor(0x14, 0x10, 0x0a), 1.4));
    QFont tickFont = p.font();
    tickFont.setPointSizeF(qMax(6.5, full.height() * 0.09));
    tickFont.setBold(true);
    p.setFont(tickFont);
    const QFontMetricsF fm(tickFont);

    auto drawTick = [&](double fraction, const QString &label, bool major) {
        const double angle = -kHalfSweepDeg + fraction * kSweepDeg;
        const QPointF outer = arcPoint(pivot, radius, angle);
        const QPointF inner = arcPoint(pivot, radius * (major ? 0.82 : 0.90), angle);
        p.drawLine(inner, outer);
        if (!label.isEmpty()) {
            const QPointF labelPos = arcPoint(pivot, radius * 0.68, angle);
            const QRectF labelRect(labelPos.x() - 14, labelPos.y() - fm.height() / 2, 28, fm.height());
            p.drawText(labelRect, Qt::AlignCenter, label);
        }
    };
    for (int s = 1; s <= 9; s += 2) {
        const double f = (double(s) / 9.0) * kSplitFraction;
        drawTick(f, QString::number(s), true);
    }
    for (int over : {20, 40, 60}) {
        const double f = kSplitFraction + (double(over) / kOverS9RangeDb) * (1.0 - kSplitFraction);
        drawTick(f, QStringLiteral("+%1").arg(over), true);
    }

    // Needle - red, pivoting from the bottom center, matching the
    // reference meter's needle color.
    const double needleAngle = -kHalfSweepDeg + needleFraction() * kSweepDeg;
    const QPointF tip = arcPoint(pivot, radius * 0.95, needleAngle);
    QPen needlePen(QColor(0xc2, 0x2a, 0x2a));
    needlePen.setWidthF(qMax(1.6, full.height() * 0.02));
    needlePen.setCapStyle(Qt::RoundCap);
    p.setPen(needlePen);
    p.drawLine(pivot, tip);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x2a, 0x2a, 0x2a));
    const double hubR = full.height() * 0.035;
    p.drawEllipse(pivot, hubR, hubR);

    // Readout below the face: bold S-value + raw dBm, same info the
    // digital meter shows, just visually anchored under the analog dial.
    QFont readoutFont = p.font();
    readoutFont.setPointSizeF(qMax(9.0, full.height() * 0.16));
    readoutFont.setBold(true);
    p.setFont(readoutFont);
    p.setPen(QColor(0xd8, 0xe6, 0xea));
    const QRectF readoutRect(full.left(), pivot.y() + full.height() * 0.02, full.width(), full.height() * 0.18);
    p.drawText(readoutRect, Qt::AlignCenter,
               sMeterText() + QStringLiteral("   %1 dBm").arg(int(m_dbm)));
}
