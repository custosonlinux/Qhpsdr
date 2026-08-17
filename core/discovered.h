#ifndef QHPSDR_DISCOVERED_H
#define QHPSDR_DISCOVERED_H

#include <QHostAddress>
#include <QString>
#include <array>
#include <cstdint>

// Device/protocol IDs and status codes, ported verbatim from deskHPSDR's
// discovered.h (core/deskhpsdr-src/discovered.h) so wire format parsing
// stays compatible with real hardware.

constexpr int MAX_DEVICES = 16;

enum HpsdrDevice {
    DEVICE_METIS = 0,
    DEVICE_HERMES = 1,
    DEVICE_GRIFFIN = 2,
    DEVICE_ANGELIA = 4,
    DEVICE_ORION = 5,
    DEVICE_HERMES_LITE = 6,
    DEVICE_HERMES_LITE2 = 506,
    DEVICE_ORION2 = 10,
    DEVICE_STEMLAB = 100,
    DEVICE_STEMLAB_Z20 = 101,

    NEW_DEVICE_ATLAS = 1000,
    NEW_DEVICE_HERMES = 1001,
    NEW_DEVICE_HERMES2 = 1002,
    NEW_DEVICE_ANGELIA = 1003,
    NEW_DEVICE_ORION = 1004,
    NEW_DEVICE_ORION2 = 1005,
    NEW_DEVICE_HERMES_LITE = 1006,
    NEW_DEVICE_SATURN = 1010,
    NEW_DEVICE_HERMES_LITE2 = 1506,
};

enum HpsdrProtocol {
    ORIGINAL_PROTOCOL = 0,
    NEW_PROTOCOL = 1,
};

enum HpsdrDeviceState {
    STATE_AVAILABLE = 2,
    STATE_SENDING = 3,
    STATE_INCOMPATIBLE = 4,
};

struct DiscoveredDevice {
    int protocol = ORIGINAL_PROTOCOL;
    int device = 0;
    int status = 0;
    int softwareVersion = 0;
    int supportedReceivers = 2;
    double frequencyMin = 0.0;
    double frequencyMax = 0.0;
    QString name;
    QHostAddress address;
    quint16 port = 0;
    std::array<quint8, 6> macAddress{};
    bool useTcp = false;
    bool useRouting = false;
};

#endif // QHPSDR_DISCOVERED_H
