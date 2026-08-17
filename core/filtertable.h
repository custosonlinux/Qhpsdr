#ifndef QHPSDR_FILTERTABLE_H
#define QHPSDR_FILTERTABLE_H

#include <QString>
#include <QVector>

#include "rxaudio.h"

// Named passband presets per mode, ported from deskHPSDR's filter tables
// (core/deskhpsdr-src/filter.c: filterLSB/filterUSB/filterAM/filterCWL/...).
// Not ported: the "ESSB" rows and the user-editable Var1/Var2 slots -
// those need actual filter-editing UI, out of scope for a fixed preset
// list.
struct FilterEntry {
    double low;
    double high;
    QString name;
};

QVector<FilterEntry> filtersForMode(RxMode mode);

// Index into filtersForMode(mode) used as that mode's default (matches
// what RxAudioChannel::applyDefaultPassband() used before per-mode
// selection existed: "2.9k" for LSB/USB, "5.2k" for AM/DSB/SAM/SPEC/DRM,
// "500" for CWL/CWU).
int defaultFilterIndexForMode(RxMode mode);

#endif // QHPSDR_FILTERTABLE_H
