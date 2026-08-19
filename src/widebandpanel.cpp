#include "widebandpanel.h"

#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>

#include "devicepanel.h"
#include "panadapterwidget.h"
#include "waterfallwidget.h"

WidebandPanel::WidebandPanel(QWidget *parent) : QWidget(parent) {
    m_devicePanel = new DevicePanel(this);
    m_panadapter = new PanadapterWidget(this);
    m_waterfall = new WaterfallWidget(this);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_panadapter);
    splitter->addWidget(m_waterfall);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_devicePanel);
    layout->addWidget(splitter, /*stretch=*/1);
}

void WidebandPanel::setSpectrum(const QVector<float> &magnitudesDb, double sampleRateHz) {
    if (magnitudesDb.isEmpty()) {
        return;
    }
    float frameMin = magnitudesDb[0];
    float frameMax = magnitudesDb[0];
    for (float v : magnitudesDb) {
        frameMin = std::min(frameMin, v);
        frameMax = std::max(frameMax, v);
    }
    m_panadapter->setDbRange(frameMin - 5.0f, frameMax + 8.0f);
    m_waterfall->setDbRange(frameMin - 5.0f, frameMax + 8.0f);
    m_panadapter->setCenterFrequencyHz(sampleRateHz / 2.0);
    m_panadapter->setSampleRateHz(sampleRateHz);
    m_panadapter->setSpectrum(magnitudesDb);
    m_waterfall->pushSpectrum(magnitudesDb);
}
