#include "waterfallwidget.h"

#include <QPainter>
#include <cstring>

WaterfallWidget::WaterfallWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(80);
}

void WaterfallWidget::setDbRange(float minDb, float maxDb) {
    m_minDb = minDb;
    m_maxDb = maxDb;
}

void WaterfallWidget::setHistoryRows(int rows) {
    m_historyRows = qMax(1, rows);
    m_image = QImage();
}

void WaterfallWidget::ensureImage() {
    if (m_image.isNull()) {
        m_image = QImage(1, m_historyRows, QImage::Format_RGB32);
        m_image.fill(Qt::black);
    }
}

QRgb WaterfallWidget::colorForDb(float db) const {
    // Classic waterfall gradient: black -> blue -> cyan -> green -> yellow -> red.
    const float t = qBound(0.0f, (db - m_minDb) / (m_maxDb - m_minDb), 1.0f);
    struct Stop {
        float pos;
        int r, g, b;
    };
    static const Stop stops[] = {
        {0.00f, 0, 0, 0},     {0.20f, 8, 8, 90},   {0.40f, 10, 120, 180},
        {0.60f, 40, 200, 90}, {0.80f, 235, 220, 40}, {1.00f, 235, 40, 30},
    };
    for (int i = 1; i < int(sizeof(stops) / sizeof(stops[0])); ++i) {
        if (t <= stops[i].pos) {
            const Stop &a = stops[i - 1];
            const Stop &b = stops[i];
            const float span = b.pos - a.pos;
            const float u = span > 0 ? (t - a.pos) / span : 0.0f;
            const int r = int(a.r + u * (b.r - a.r));
            const int g = int(a.g + u * (b.g - a.g));
            const int bch = int(a.b + u * (b.b - a.b));
            return qRgb(r, g, bch);
        }
    }
    return qRgb(stops[std::size(stops) - 1].r, stops[std::size(stops) - 1].g, stops[std::size(stops) - 1].b);
}

void WaterfallWidget::pushSpectrum(const QVector<float> &magnitudesDb) {
    if (magnitudesDb.isEmpty()) {
        return;
    }
    if (m_image.isNull() || m_image.width() != magnitudesDb.size()) {
        m_image = QImage(magnitudesDb.size(), m_historyRows, QImage::Format_RGB32);
        m_image.fill(Qt::black);
    }

    // Scroll everything down by one row, then paint the new frame into row 0.
    uchar *bits = m_image.bits();
    const int bpl = m_image.bytesPerLine();
    std::memmove(bits + bpl, bits, size_t(bpl) * size_t(m_image.height() - 1));

    QRgb *row = reinterpret_cast<QRgb *>(m_image.scanLine(0));
    for (int x = 0; x < magnitudesDb.size(); ++x) {
        row[x] = colorForDb(magnitudesDb[x]);
    }

    update();
}

void WaterfallWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    ensureImage();
}

void WaterfallWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    if (m_image.isNull()) {
        p.fillRect(rect(), Qt::black);
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(rect(), m_image, m_image.rect());
}
