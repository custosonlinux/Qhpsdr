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

// Amateur-radio band-plan convention: LSB below 10MHz (160/80/60/40m),
// USB at/above it (30m and up) - the standard split used across the HF
// bands. Used to pick a sensible default mode when tuning to a new
// frequency/band, e.g. VfoPanel's initial state and ToolbarWidget's band
// buttons - not applied on every retune, so it never overrides a mode the
// user picked deliberately.
RxMode defaultModeForFrequency(double hz);

#endif // QHPSDR_FILTERTABLE_H
