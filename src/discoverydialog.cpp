#include "discoverydialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QHostInfo>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "discovery.h"

namespace {

QString protocolName(int protocol) {
    return protocol == NEW_PROTOCOL ? QStringLiteral("P2") : QStringLiteral("P1");
}

QString macToString(const std::array<quint8, 6> &mac) {
    QStringList parts;
    for (quint8 b : mac) {
        parts << QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
    }
    return parts.join(QLatin1Char(':'));
}

} // namespace

DiscoveryDialog::DiscoveryDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Discover HPSDR Devices"));
    resize(560, 360);

    m_service = new DiscoveryService(this);
    connect(m_service, &DiscoveryService::deviceFound, this, &DiscoveryDialog::onDeviceFound);
    connect(m_service, &DiscoveryService::finished, this, &DiscoveryDialog::onDiscoveryFinished);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Protocol"), tr("Address"), tr("MAC"), tr("Status")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { onRowActivated(row); });

    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setPlaceholderText(tr("Radio IP address (optional)"));

    m_portEdit = new QLineEdit(this);
    m_portEdit->setText(QStringLiteral("1024"));
    m_portEdit->setValidator(new QIntValidator(1, 65535, m_portEdit));
    m_portEdit->setFixedWidth(60);
    m_portEdit->setToolTip(tr("Port (default 1024)"));

    m_protocolCombo = new QComboBox(this);
    m_protocolCombo->addItem(tr("Protocol 1"), ORIGINAL_PROTOCOL);
    m_protocolCombo->addItem(tr("Protocol 2"), NEW_PROTOCOL);
    m_protocolCombo->setToolTip(
        tr("Which protocol the radio at this address speaks - discovery infers this automatically, "
           "but a direct-by-IP connect has no reply to read it from."));

    m_discoverButton = new QPushButton(tr("Discover"), this);
    connect(m_discoverButton, &QPushButton::clicked, this, &DiscoveryDialog::startDiscovery);

    m_connectDirectButton = new QPushButton(tr("Connect Directly"), this);
    m_connectDirectButton->setToolTip(
        tr("Skip discovery and connect straight to this address.\n"
           "Use this when the radio can't be discovered - e.g. over a VPN, where discovery's UDP "
           "broadcast replies don't get routed back, even though the actual data connection is "
           "unicast and works fine."));
    connect(m_connectDirectButton, &QPushButton::clicked, this, &DiscoveryDialog::connectDirectly);

    m_connectButton = new QPushButton(tr("Connect"), this);
    m_connectButton->setEnabled(false);
    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
        const int row = m_table->currentRow();
        if (row >= 0) {
            onRowActivated(row);
        }
    });

    auto *cancelButton = new QPushButton(tr("Cancel"), this);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    m_statusLabel = new QLabel(tr("Press \"Discover\" to search for devices."), this);

    // Favorites: saved "Connect Directly" addresses so a VPN-only radio
    // (see connectDirectly()'s tooltip) doesn't need its IP retyped every
    // time. Picking one from the dropdown fills the host/port fields and
    // connects immediately - a single click, as requested.
    m_favoritesCombo = new QComboBox(this);
    m_favoritesCombo->setToolTip(tr("Saved addresses - pick one to connect immediately."));
    connect(m_favoritesCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (index <= 0 || index > m_favorites.size()) {
            return;
        }
        const DeviceFavorite &fav = m_favorites.at(index - 1);
        m_hostEdit->setText(fav.host);
        m_portEdit->setText(QString::number(fav.port));
        m_protocolCombo->setCurrentIndex(m_protocolCombo->findData(fav.protocol));
        connectDirectly();
    });

    m_saveFavoriteButton = new QPushButton(tr("Save As Favorite..."), this);
    m_saveFavoriteButton->setToolTip(tr("Save the address above as a favorite for quick reconnecting."));
    connect(m_saveFavoriteButton, &QPushButton::clicked, this, [this]() {
        const QString host = m_hostEdit->text().trimmed();
        if (host.isEmpty()) {
            m_statusLabel->setText(tr("Enter an IP address or hostname first."));
            return;
        }
        QString name, description;
        if (!promptFavoriteDetails(QString(), QString(), name, description)) {
            return;
        }
        DeviceFavorite fav;
        fav.name = name;
        fav.description = description;
        fav.host = host;
        fav.port = quint16(m_portEdit->text().toUInt());
        fav.protocol = m_protocolCombo->currentData().toInt();
        m_favorites.append(fav);
        saveFavorites();
        refreshFavoritesCombo();
    });

    m_manageFavoritesButton = new QPushButton(tr("Manage..."), this);
    connect(m_manageFavoritesButton, &QPushButton::clicked, this, &DiscoveryDialog::manageFavorites);

    auto *favoritesLayout = new QHBoxLayout;
    favoritesLayout->addWidget(new QLabel(tr("Favorites:"), this));
    favoritesLayout->addWidget(m_favoritesCombo, /*stretch=*/1);
    favoritesLayout->addWidget(m_saveFavoriteButton);
    favoritesLayout->addWidget(m_manageFavoritesButton);

    auto *hostLayout = new QHBoxLayout;
    hostLayout->addWidget(m_hostEdit);
    hostLayout->addWidget(m_portEdit);
    hostLayout->addWidget(m_protocolCombo);
    hostLayout->addWidget(m_connectDirectButton);
    hostLayout->addWidget(m_discoverButton);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_statusLabel);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_connectButton);
    buttonLayout->addWidget(cancelButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(favoritesLayout);
    layout->addLayout(hostLayout);
    layout->addWidget(m_table);
    layout->addLayout(buttonLayout);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_connectButton->setEnabled(m_table->currentRow() >= 0);
    });

    loadFavorites();
    refreshFavoritesCombo();

    startDiscovery();
}

void DiscoveryDialog::startDiscovery() {
    m_table->setRowCount(0);
    m_rowDevices.clear();
    m_connectButton->setEnabled(false);
    m_statusLabel->setText(tr("Searching..."));
    m_discoverButton->setEnabled(false);
    m_service->start(m_hostEdit->text().trimmed());
}

void DiscoveryDialog::onDeviceFound(const DiscoveredDevice &device) {
    addRow(device);
}

void DiscoveryDialog::onDiscoveryFinished() {
    m_discoverButton->setEnabled(true);
    m_statusLabel->setText(m_rowDevices.isEmpty() ? tr("No devices found.")
                                                   : tr("Found %1 device(s).").arg(m_rowDevices.size()));
}

void DiscoveryDialog::connectDirectly() {
    const QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) {
        m_statusLabel->setText(tr("Enter an IP address or hostname first."));
        return;
    }

    QHostAddress address(host);
    if (address.isNull()) {
        // Not a literal IP - try resolving it as a hostname.
        const QHostInfo info = QHostInfo::fromName(host);
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            m_statusLabel->setText(tr("Could not resolve \"%1\".").arg(host));
            return;
        }
        address = info.addresses().constFirst();
    }

    DiscoveredDevice device;
    device.protocol = m_protocolCombo->currentData().toInt();
    device.address = address;
    device.port = quint16(m_portEdit->text().toUInt());
    device.name = tr("%1 (direct)").arg(host);
    device.status = STATE_AVAILABLE;

    m_selected = device;
    accept();
}

void DiscoveryDialog::onRowActivated(int row) {
    if (row < 0 || row >= m_rowDevices.size()) {
        return;
    }
    m_selected = m_rowDevices.at(row);
    accept();
}

void DiscoveryDialog::addRow(const DiscoveredDevice &device) {
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(device.name));
    m_table->setItem(row, 1, new QTableWidgetItem(protocolName(device.protocol)));
    m_table->setItem(row, 2, new QTableWidgetItem(device.address.toString()));
    m_table->setItem(row, 3, new QTableWidgetItem(macToString(device.macAddress)));
    m_table->setItem(row, 4, new QTableWidgetItem(device.status == STATE_SENDING ? tr("In use") : tr("Available")));
    m_rowDevices.append(device);
}

void DiscoveryDialog::loadFavorites() {
    QSettings settings;
    const int count = settings.beginReadArray(QStringLiteral("favorites"));
    m_favorites.clear();
    m_favorites.reserve(count);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        DeviceFavorite fav;
        fav.name = settings.value(QStringLiteral("name")).toString();
        fav.description = settings.value(QStringLiteral("description")).toString();
        fav.host = settings.value(QStringLiteral("host")).toString();
        fav.port = quint16(settings.value(QStringLiteral("port"), 1024).toUInt());
        fav.protocol = settings.value(QStringLiteral("protocol"), ORIGINAL_PROTOCOL).toInt();
        m_favorites.append(fav);
    }
    settings.endArray();
}

void DiscoveryDialog::saveFavorites() {
    QSettings settings;
    settings.beginWriteArray(QStringLiteral("favorites"));
    for (int i = 0; i < m_favorites.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), m_favorites.at(i).name);
        settings.setValue(QStringLiteral("description"), m_favorites.at(i).description);
        settings.setValue(QStringLiteral("host"), m_favorites.at(i).host);
        settings.setValue(QStringLiteral("port"), m_favorites.at(i).port);
        settings.setValue(QStringLiteral("protocol"), m_favorites.at(i).protocol);
    }
    settings.endArray();
}

void DiscoveryDialog::refreshFavoritesCombo() {
    const QSignalBlocker blocker(m_favoritesCombo);
    m_favoritesCombo->clear();
    m_favoritesCombo->addItem(tr("(choose a favorite)"));
    for (const auto &fav : m_favorites) {
        m_favoritesCombo->addItem(fav.description.isEmpty() ? fav.name
                                                              : tr("%1 - %2").arg(fav.name, fav.description));
    }
    m_favoritesCombo->setCurrentIndex(0);
}

bool DiscoveryDialog::promptFavoriteDetails(const QString &initialName, const QString &initialDescription,
                                             QString &nameOut, QString &descriptionOut) {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Save Favorite"));
    auto *nameEdit = new QLineEdit(initialName, &dlg);
    nameEdit->setPlaceholderText(tr("e.g. Home Shack"));
    auto *descriptionEdit = new QLineEdit(initialDescription, &dlg);
    descriptionEdit->setPlaceholderText(tr("e.g. Hermes via VPN"));

    auto *form = new QFormLayout;
    form->addRow(tr("Name:"), nameEdit);
    form->addRow(tr("Description:"), descriptionEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);

    nameEdit->setFocus();
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    nameOut = nameEdit->text().trimmed();
    descriptionOut = descriptionEdit->text().trimmed();
    return !nameOut.isEmpty();
}

void DiscoveryDialog::manageFavorites() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Manage Favorites"));
    dlg.resize(420, 280);

    auto *list = new QListWidget(&dlg);
    for (const auto &fav : m_favorites) {
        list->addItem(tr("%1 - %2 (%3:%4)").arg(fav.name, fav.description, fav.host).arg(fav.port));
    }

    auto *removeButton = new QPushButton(tr("Remove"), &dlg);
    removeButton->setEnabled(false);
    connect(list, &QListWidget::currentRowChanged, &dlg, [removeButton](int row) { removeButton->setEnabled(row >= 0); });
    connect(removeButton, &QPushButton::clicked, &dlg, [this, list, removeButton]() {
        const int row = list->currentRow();
        if (row < 0 || row >= m_favorites.size()) {
            return;
        }
        m_favorites.removeAt(row);
        delete list->takeItem(row);
        removeButton->setEnabled(list->currentRow() >= 0);
    });

    auto *closeButton = new QPushButton(tr("Close"), &dlg);
    connect(closeButton, &QPushButton::clicked, &dlg, &QDialog::accept);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(removeButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(list);
    layout->addLayout(buttonLayout);

    dlg.exec();

    saveFavorites();
    refreshFavoritesCombo();
}
