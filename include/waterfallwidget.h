#ifndef QHPSDR_WATERFALLWIDGET_H
#define QHPSDR_WATERFALLWIDGET_H

#include <QImage>
#include <QVector>
#include <QWidget>

// Scrolling spectrogram, styled after deskHPSDR's waterfall
// (core/deskhpsdr-src/waterfall.c): each new spectrum frame becomes a new
// row at the top, older rows scroll down, color-mapped blue -> cyan ->
// green -> yellow -> red by level. Shares PanadapterWidget's frequency
// axis (see the project's overall panadapter/waterfall stacking in
// core/deskhpsdr-src/radio.c's radio_create_visual()).
class WaterfallWidget : public QWidget {
    Q_OBJECT

public:
    explicit WaterfallWidget(QWidget *parent = nullptr);

    void pushSpectrum(const QVector<float> &magnitudesDb);
    void setDbRange(float minDb, float maxDb);
    void setHistoryRows(int rows);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QRgb colorForDb(float db) const;
    void ensureImage();

    QImage m_image;
    int m_historyRows = 256;
    float m_minDb = -140.0f;
    float m_maxDb = -20.0f;
};

#endif // QHPSDR_WATERFALLWIDGET_H
