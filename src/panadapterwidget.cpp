#include "panadapterwidget.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace {
constexpr int kLeftMargin = 46;
constexpr int kTopMargin = 18;

// "Nice" grid step selection, same idea as any axis-labeling code: pick the
// smallest of 1/2/5 * 10^n that's still >= the minimum pixel spacing we want
// between labels.
double niceStep(double roughStep) {
    const double mag = std::pow(10.0, std::floor(std::log10(roughStep)));
    const double residual = roughStep / mag;
    double step;
    if (residual > 5.0) {
        step = 10.0;
    } else if (residual > 2.0) {
        step = 5.0;
    } else if (residual > 1.0) {
        step = 2.0;
    } else {
        step = 1.0;
    }
    return step * mag;
}
} // namespace

PanadapterWidget::PanadapterWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(160);
    setAutoFillBackground(false);
}

void PanadapterWidget::setSpectrum(const QVector<float> &magnitudesDb) {
    m_spectrum = magnitudesDb;
    update();
}

void PanadapterWidget::setCenterFrequencyHz(double hz) {
    m_centerHz = hz;
    update();
}

void PanadapterWidget::setSampleRateHz(double hz) {
    m_sampleRateHz = hz;
    update();
}

void PanadapterWidget::setDbRange(float minDb, float maxDb) {
    m_minDb = minDb;
    m_maxDb = maxDb;
    update();
}

void PanadapterWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect full = rect();
    const QRect plot(kLeftMargin, kTopMargin, full.width() - kLeftMargin - 4, full.height() - kTopMargin - 4);

    p.fillRect(full, QColor(0x08, 0x10, 0x1a));
    p.fillRect(plot, QColor(0x0d, 0x1b, 0x2a));

    if (plot.width() <= 0 || plot.height() <= 0) {
        return;
    }

    const double loHz = m_centerHz - m_sampleRateHz / 2.0;
    const double hiHz = m_centerHz + m_sampleRateHz / 2.0;

    // --- dB grid + labels ---------------------------------------------
    p.setPen(QColor(0x1c, 0x35, 0x45));
    QFont axisFont = p.font();
    axisFont.setPointSizeF(axisFont.pointSizeF() * 0.85);
    p.setFont(axisFont);

    const double dbStep = niceStep((m_maxDb - m_minDb) / (plot.height() / 40.0));
    const double firstDb = std::ceil(m_minDb / dbStep) * dbStep;
    for (double db = firstDb; db <= m_maxDb; db += dbStep) {
        const double t = (db - m_minDb) / (m_maxDb - m_minDb);
        const int y = plot.bottom() - int(t * plot.height());
        p.drawLine(plot.left(), y, plot.right(), y);
        p.setPen(QColor(0x6f, 0xc7, 0xd8));
        p.drawText(QRect(0, y - 8, kLeftMargin - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(int(db)));
        p.setPen(QColor(0x1c, 0x35, 0x45));
    }

    // --- frequency grid + labels ---------------------------------------
    const double hzStep = niceStep(m_sampleRateHz / (plot.width() / 90.0));
    const double firstHz = std::ceil(loHz / hzStep) * hzStep;
    for (double f = firstHz; f <= hiHz; f += hzStep) {
        const double t = (f - loHz) / (hiHz - loHz);
        const int x = plot.left() + int(t * plot.width());
        p.setPen(QColor(0x1c, 0x35, 0x45));
        p.drawLine(x, plot.top(), x, plot.bottom());
        p.setPen(QColor(0x6f, 0xc7, 0xd8));
        p.drawText(QRect(x - 40, 1, 80, kTopMargin - 2), Qt::AlignHCenter | Qt::AlignVCenter,
                   QString::number(f / 1e6, 'f', 3));
    }

    // --- tuned-frequency cursor -----------------------------------------
    {
        const double t = (m_centerHz - loHz) / (hiHz - loHz);
        const int x = plot.left() + int(t * plot.width());
        p.setPen(QPen(QColor(0xd0, 0x30, 0x30), 1));
        p.drawLine(x, plot.top(), x, plot.bottom());
    }

    if (m_spectrum.isEmpty()) {
        return;
    }

    // --- spectrum trace ---------------------------------------------------
    QPainterPath path;
    const int n = m_spectrum.size();
    auto yFor = [&](float db) {
        const float t = qBound(0.0f, (db - m_minDb) / (m_maxDb - m_minDb), 1.0f);
        return plot.bottom() - t * plot.height();
    };
    path.moveTo(plot.left(), plot.bottom());
    for (int px = 0; px < plot.width(); ++px) {
        const int bin = qBound(0, int(double(px) / plot.width() * n), n - 1);
        path.lineTo(plot.left() + px, yFor(m_spectrum[bin]));
    }
    path.lineTo(plot.right(), plot.bottom());
    path.closeSubpath();

    QLinearGradient fillGrad(0, plot.top(), 0, plot.bottom());
    fillGrad.setColorAt(0.0, QColor(0xd6, 0xe0, 0x40, 235));
    fillGrad.setColorAt(0.55, QColor(0x7c, 0xc4, 0x3a, 220));
    fillGrad.setColorAt(1.0, QColor(0x1a, 0x3a, 0x2a, 60));
    p.setPen(Qt::NoPen);
    p.setBrush(fillGrad);
    p.drawPath(path);

    p.setPen(QPen(QColor(0xf2, 0xf6, 0xb0), 1));
    p.setBrush(Qt::NoBrush);
    QPainterPath outline;
    outline.moveTo(plot.left(), yFor(m_spectrum[0]));
    for (int px = 1; px < plot.width(); ++px) {
        const int bin = qBound(0, int(double(px) / plot.width() * n), n - 1);
        outline.lineTo(plot.left() + px, yFor(m_spectrum[bin]));
    }
    p.drawPath(outline);

    p.setPen(QColor(0x40, 0x60, 0x70));
    p.drawRect(plot.adjusted(0, 0, -1, -1));
}

void PanadapterWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QRect full = rect();
    const QRect plot(kLeftMargin, kTopMargin, full.width() - kLeftMargin - 4, full.height() - kTopMargin - 4);
    if (plot.width() <= 0 || !plot.contains(event->pos())) {
        return;
    }
    const double loHz = m_centerHz - m_sampleRateHz / 2.0;
    const double hiHz = m_centerHz + m_sampleRateHz / 2.0;
    const double t = double(event->pos().x() - plot.left()) / double(plot.width());
    emit frequencyClicked(loHz + t * (hiHz - loHz));
}
