#include "filtertable.h"

namespace {

// core/deskhpsdr-src/filter.c: filterLSB[]/filterUSB[].
const QVector<FilterEntry> &lsbFilters() {
    static const QVector<FilterEntry> table = {
        {-5150, -150, "5.0k"}, {-4550, -150, "4.4k"}, {-3950, -150, "3.8k"}, {-3450, -150, "3.3k"},
        {-3050, -150, "2.9k"}, {-2850, -150, "2.7k"}, {-2550, -150, "2.4k"}, {-2250, -150, "2.1k"},
        {-1950, -150, "1.8k"}, {-1150, -150, "1.0k"},
    };
    return table;
}

const QVector<FilterEntry> &usbFilters() {
    static const QVector<FilterEntry> table = {
        {150, 5150, "5.0k"}, {150, 4550, "4.4k"}, {150, 3950, "3.8k"}, {150, 3450, "3.3k"},
        {150, 3050, "2.9k"}, {150, 2850, "2.7k"}, {150, 2550, "2.4k"}, {150, 2250, "2.1k"},
        {150, 1950, "1.8k"}, {150, 1150, "1.0k"},
    };
    return table;
}

// core/deskhpsdr-src/filter.c: filterAM[]/filterSAM[]/filterDSB[]/
// filterSPEC[]/filterDRM[] (identical apart from the trailing
// placeholder/Var1/Var2 rows we don't port).
const QVector<FilterEntry> &symmetricWideFilters() {
    static const QVector<FilterEntry> table = {
        {-8000, 8000, "16k"}, {-6000, 6000, "12k"}, {-5000, 5000, "10k"}, {-4000, 4000, "8k"},
        {-3300, 3300, "6.6k"}, {-2600, 2600, "5.2k"}, {-2000, 2000, "4.0k"}, {-1550, 1550, "3.1k"},
        {-1450, 1450, "2.9k"}, {-1200, 1200, "2.4k"},
    };
    return table;
}

// core/deskhpsdr-src/filter.c: filterCWL[]/filterCWU[].
const QVector<FilterEntry> &cwFilters() {
    static const QVector<FilterEntry> table = {
        {-500, 500, "1.0k"}, {-400, 400, "800"}, {-375, 375, "750"}, {-300, 300, "600"}, {-250, 250, "500"},
        {-200, 200, "400"},  {-125, 125, "250"}, {-50, 50, "100"},   {-25, 25, "50"},    {-13, 13, "25"},
    };
    return table;
}

const QVector<FilterEntry> &wideDigitalFilters() {
    static const QVector<FilterEntry> table = {
        {-1500, 1500, "1.5k"},
        {-2600, 2600, "2.6k"},
        {-4000, 4000, "4.0k"},
    };
    return table;
}

const QVector<FilterEntry> &fmFilters() {
    // deskHPSDR: "This FMN filter edges are nowhere used" - selectivity
    // comes from the FM demodulator's deviation setting, not the
    // bandpass filter, so there's nothing meaningful to list here.
    static const QVector<FilterEntry> table = {{-8000, 8000, "Wide"}};
    return table;
}

} // namespace

QVector<FilterEntry> filtersForMode(RxMode mode) {
    switch (mode) {
    case RxMode::LSB:
        return lsbFilters();
    case RxMode::USB:
        return usbFilters();
    case RxMode::DSB:
    case RxMode::AM:
    case RxMode::SAM:
    case RxMode::SPEC:
    case RxMode::DRM:
        return symmetricWideFilters();
    case RxMode::CWL:
    case RxMode::CWU:
        return cwFilters();
    case RxMode::DIGL:
    case RxMode::DIGU:
        return wideDigitalFilters();
    case RxMode::FM:
    case RxMode::WBFM:
        return fmFilters();
    }
    return symmetricWideFilters();
}

int defaultFilterIndexForMode(RxMode mode) {
    switch (mode) {
    case RxMode::LSB:
    case RxMode::USB:
        return 4; // "2.9k"
    case RxMode::CWL:
    case RxMode::CWU:
        return 4; // "500"
    case RxMode::DSB:
    case RxMode::AM:
    case RxMode::SAM:
    case RxMode::SPEC:
    case RxMode::DRM:
        return 5; // "5.2k"
    case RxMode::DIGL:
    case RxMode::DIGU:
    case RxMode::FM:
    case RxMode::WBFM:
        return 0;
    }
    return 0;
}
