#ifndef QHPSDR_PANADAPTERWIDGET_H
#define QHPSDR_PANADAPTERWIDGET_H

#include <QElapsedTimer>
#include <QLinearGradient>
#include <QPixmap>
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

    // Shades the currently selected demod passband (VfoPanel's filter
    // combo, core/filtertable.h's FilterEntry::low/high) around the tuned
    // frequency, like deskHPSDR's rx_panadapter.c filter overlay. lowHz/
    // highHz are offsets from the tuned frequency (can be negative), not
    // absolute - e.g. LSB's "2.9k" is roughly -3050..-150.
    void setPassband(double lowHz, double highHz);

    // Decaying max-hold overlay per bin, styled after deskHPSDR's Display
    // menu "Peak Blobs & Hold" (Decay hold time/Drop dBm/s) - see
    // SettingsDialog's Display tab. Off by default. A bin's peak stays put
    // for holdTimeSec after last being topped up, then falls at
    // dropDbPerSec until either overtaken by a fresh reading or it decays
    // below the current dB floor.
    void setPeakHoldEnabled(bool enabled);
    void setPeakHoldParams(double holdTimeSec, double dropDbPerSec);

signals:
    // Emitted on a left click inside the plot area, converted from pixel
    // position to frequency using the same loHz/hiHz mapping paintEvent()
    // draws the axis with.
    void frequencyClicked(double hz);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    // The dB/frequency grid and their axis labels only depend on the plot
    // geometry, center frequency, sample rate, and dB range - not on the
    // spectrum data itself, which arrives many times a second. Redrawing
    // ~100 grid lines and text labels (expensive on the software rasterizer)
    // on every incoming spectrum frame was the single largest cost in
    // paintEvent(); caching them into a QPixmap and only regenerating on an
    // actual geometry/range/frequency change turns that into a cheap blit.
    void rebuildGridCache(const QRect &full, const QRect &plot);

    QVector<float> m_spectrum;

    // Peak-hold state - see setPeakHoldEnabled()/setPeakHoldParams().
    // m_peakHold/m_peakHoldAgeMs are resized (and reset) whenever the
    // incoming spectrum's bin count changes (e.g. a zoom change), since
    // per-bin state stops being meaningful across a different crop.
    bool m_peakHoldEnabled = false;
    double m_peakHoldTimeSec = 2.5;
    double m_peakHoldDropDbPerSec = 6.0;
    QVector<float> m_peakHold;
    QVector<qint64> m_peakHoldAgeMs;
    QElapsedTimer m_peakHoldClock;

    double m_centerHz = 7100000.0;
    double m_sampleRateHz = 48000.0;
    float m_minDb = -140.0f;
    float m_maxDb = -20.0f;
    double m_passbandLowHz = 0.0;
    double m_passbandHighHz = 0.0;

    QPixmap m_gridCache;
    QRect m_gridCachePlot;
    double m_gridCacheCenterHz = 0.0;
    double m_gridCacheSampleRateHz = 0.0;
    float m_gridCacheMinDb = 0.0f;
    float m_gridCacheMaxDb = 0.0f;
    bool m_gridCacheValid = false;

    // Reused across paintEvent() calls so the QLinearGradient (and its
    // Qt-internal color-ramp cache) is only rebuilt when the plot height
    // actually changes, not on every frame.
    QLinearGradient m_fillGradient;
    int m_fillGradientHeight = -1;
};

#endif // QHPSDR_PANADAPTERWIDGET_H
