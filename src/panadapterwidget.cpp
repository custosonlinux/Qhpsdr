#include "panadapterwidget.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
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

    if (m_peakHoldEnabled && !m_spectrum.isEmpty()) {
        if (m_peakHold.size() != m_spectrum.size()) {
            // Bin count changed (e.g. zoom) - per-bin peak state no longer
            // means anything, start fresh at this frame's own values.
            m_peakHold = m_spectrum;
            m_peakHoldAgeMs.assign(m_spectrum.size(), 0);
            m_peakHoldClock.start();
        } else {
            const qint64 dtMs = m_peakHoldClock.isValid() ? m_peakHoldClock.restart() : 0;
            const double dropThisFrame = m_peakHoldDropDbPerSec * (double(dtMs) / 1000.0);
            const qint64 holdTimeMs = qint64(m_peakHoldTimeSec * 1000.0);
            for (int i = 0; i < m_spectrum.size(); ++i) {
                if (m_spectrum[i] >= m_peakHold[i]) {
                    m_peakHold[i] = m_spectrum[i];
                    m_peakHoldAgeMs[i] = 0;
                } else {
                    m_peakHoldAgeMs[i] += dtMs;
                    if (m_peakHoldAgeMs[i] > holdTimeMs) {
                        m_peakHold[i] = qMax(double(m_spectrum[i]), m_peakHold[i] - dropThisFrame);
                    }
                }
            }
        }
    }
    update();
}

void PanadapterWidget::setPeakHoldEnabled(bool enabled) {
    m_peakHoldEnabled = enabled;
    if (!enabled) {
        m_peakHold.clear();
        m_peakHoldAgeMs.clear();
    }
    update();
}

void PanadapterWidget::setPeakHoldParams(double holdTimeSec, double dropDbPerSec) {
    m_peakHoldTimeSec = holdTimeSec;
    m_peakHoldDropDbPerSec = dropDbPerSec;
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

void PanadapterWidget::setPassband(double lowHz, double highHz) {
    m_passbandLowHz = lowHz;
    m_passbandHighHz = highHz;
    update();
}

void PanadapterWidget::rebuildGridCache(const QRect &full, const QRect &plot) {
    m_gridCache = QPixmap(full.size());
    QPainter p(&m_gridCache);
    p.setRenderHint(QPainter::Antialiasing, false);

    p.fillRect(full, QColor(0x08, 0x10, 0x1a));
    p.fillRect(plot, QColor(0x0d, 0x1b, 0x2a));

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

    m_gridCachePlot = plot;
    m_gridCacheCenterHz = m_centerHz;
    m_gridCacheSampleRateHz = m_sampleRateHz;
    m_gridCacheMinDb = m_minDb;
    m_gridCacheMaxDb = m_maxDb;
    m_gridCacheValid = true;
}

void PanadapterWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect full = rect();
    const QRect plot(kLeftMargin, kTopMargin, full.width() - kLeftMargin - 4, full.height() - kTopMargin - 4);

    if (plot.width() <= 0 || plot.height() <= 0) {
        return;
    }

    const bool cacheStale = !m_gridCacheValid || m_gridCache.size() != full.size() || m_gridCachePlot != plot ||
                             m_gridCacheCenterHz != m_centerHz || m_gridCacheSampleRateHz != m_sampleRateHz ||
                             m_gridCacheMinDb != m_minDb || m_gridCacheMaxDb != m_maxDb;
    if (cacheStale) {
        rebuildGridCache(full, plot);
    }
    p.drawPixmap(0, 0, m_gridCache);

    const double loHz = m_centerHz - m_sampleRateHz / 2.0;
    const double hiHz = m_centerHz + m_sampleRateHz / 2.0;

    // --- selected passband shading ---------------------------------------
    if (m_passbandHighHz > m_passbandLowHz) {
        const double passLoHz = m_centerHz + m_passbandLowHz;
        const double passHiHz = m_centerHz + m_passbandHighHz;
        const double tLo = (passLoHz - loHz) / (hiHz - loHz);
        const double tHi = (passHiHz - loHz) / (hiHz - loHz);
        const int xLo = plot.left() + int(qBound(0.0, tLo, 1.0) * plot.width());
        const int xHi = plot.left() + int(qBound(0.0, tHi, 1.0) * plot.width());
        if (xHi > xLo) {
            p.fillRect(QRect(xLo, plot.top(), xHi - xLo, plot.height()), QColor(0x6f, 0xc7, 0xd8, 40));
            p.setPen(QPen(QColor(0x6f, 0xc7, 0xd8, 130), 1));
            p.drawLine(xLo, plot.top(), xLo, plot.bottom());
            p.drawLine(xHi, plot.top(), xHi, plot.bottom());
        }
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
    // Compute each pixel's (x, y) once and reuse it for both the filled
    // trace and its outline stroke, instead of running the per-pixel
    // bin-lookup/yFor loop twice (previously a second, separate
    // QPainterPath was built from scratch just for the outline).
    const int n = m_spectrum.size();
    auto yFor = [&](float db) {
        const float t = qBound(0.0f, (db - m_minDb) / (m_maxDb - m_minDb), 1.0f);
        return plot.bottom() - t * plot.height();
    };
    QPolygonF topLine(plot.width());
    for (int px = 0; px < plot.width(); ++px) {
        const int bin = qBound(0, int(double(px) / plot.width() * n), n - 1);
        topLine[px] = QPointF(plot.left() + px, yFor(m_spectrum[bin]));
    }

    // Same green -> yellow -> red palette as the S-meter (VfoPanel's
    // QProgressBar::chunk gradient, #2f8f3a/#d8c22a/#c23b2a), so strong
    // peaks read the same way in both places. plot.top() is the highest
    // displayed dB (loudest signals), plot.bottom() the noise floor.
    if (m_fillGradientHeight != plot.height()) {
        m_fillGradient = QLinearGradient(0, plot.top(), 0, plot.bottom());
        m_fillGradient.setColorAt(0.0, QColor(0xc2, 0x3b, 0x2a, 235));
        m_fillGradient.setColorAt(0.35, QColor(0xd8, 0xc2, 0x2a, 220));
        m_fillGradient.setColorAt(1.0, QColor(0x2f, 0x8f, 0x3a, 90));
        m_fillGradientHeight = plot.height();
    }
    QPainterPath fillPath;
    fillPath.addPolygon(topLine);
    fillPath.lineTo(plot.right(), plot.bottom());
    fillPath.lineTo(plot.left(), plot.bottom());
    fillPath.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(m_fillGradient);
    p.drawPath(fillPath);

    p.setPen(QPen(QColor(0xf2, 0xf6, 0xb0), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(topLine);

    // --- peak-hold overlay -------------------------------------------------
    // Plain white/gray line above the live trace - visually distinct from
    // the live yellow trace and the red/yellow/green fill, same idea as
    // deskHPSDR's PEAKS & HOLD line (default color there is also a plain,
    // neutral tone against the colored fill).
    if (m_peakHoldEnabled && m_peakHold.size() == n) {
        QPolygonF peakLine(plot.width());
        for (int px = 0; px < plot.width(); ++px) {
            const int bin = qBound(0, int(double(px) / plot.width() * n), n - 1);
            peakLine[px] = QPointF(plot.left() + px, yFor(m_peakHold[bin]));
        }
        p.setPen(QPen(QColor(0xd8, 0xe6, 0xea, 220), 1));
        p.drawPolyline(peakLine);
    }

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
