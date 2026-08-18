#ifndef QHPSDR_PANADAPTERWIDGET_H
#define QHPSDR_PANADAPTERWIDGET_H

#include <QWidget>
#include <QVector>

// Spectrum ("panadapter") display, styled after deskHPSDR's panadapter
// (core/deskhpsdr-src/rx_panadapter.c): dark navy background, cyan grid,
// a filled yellow-green trace, dB scale on the left and a frequency scale
// along the top, with a marker for the tuned frequency. Plain QPainter for
// now rather than OpenGL - see the project README's OpenGL goal; this gets
// a correct, working display up first, GPU rendering is a follow-up.
class PanadapterWidget : public QWidget {
    Q_OBJECT

public:
    explicit PanadapterWidget(QWidget *parent = nullptr);

    void setSpectrum(const QVector<float> &magnitudesDb);
    void setCenterFrequencyHz(double hz);
    void setSampleRateHz(double hz);
    void setDbRange(float minDb, float maxDb);

signals:
    // Emitted on a left click inside the plot area, converted from pixel
    // position to frequency using the same loHz/hiHz mapping paintEvent()
    // draws the axis with.
    void frequencyClicked(double hz);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QVector<float> m_spectrum;
    double m_centerHz = 7100000.0;
    double m_sampleRateHz = 48000.0;
    float m_minDb = -140.0f;
    float m_maxDb = -20.0f;
};

#endif // QHPSDR_PANADAPTERWIDGET_H
