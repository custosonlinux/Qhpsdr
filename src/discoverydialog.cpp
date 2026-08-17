#include "discoverydialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
    m_hostEdit->setPlaceholderText(tr("Radio IP address (optional, for targeted discovery)"));

    m_discoverButton = new QPushButton(tr("Discover"), this);
    connect(m_discoverButton, &QPushButton::clicked, this, &DiscoveryDialog::startDiscovery);

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

    auto *hostLayout = new QHBoxLayout;
    hostLayout->addWidget(m_hostEdit);
    hostLayout->addWidget(m_discoverButton);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_statusLabel);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_connectButton);
    buttonLayout->addWidget(cancelButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(hostLayout);
    layout->addWidget(m_table);
    layout->addLayout(buttonLayout);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_connectButton->setEnabled(m_table->currentRow() >= 0);
    });

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
