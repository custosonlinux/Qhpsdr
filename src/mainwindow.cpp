#include "mainwindow.h"

#include <QAction>
#include <QAudioFormat>
#include <QAudioSink>
#include <QApplication>
#include <QDockWidget>
#include <QMediaDevices>
#include <QMenuBar>
#include <QMetaObject>
#include <QSettings>
#include <QStatusBar>
#include <QThread>

#include "analogmeterwidget.h"
#include "devicepanel.h"
#include "discoverydialog.h"
#include "newprotocol.h"
#include "oldprotocol.h"
#include "panadapterwidget.h"
#include "radioconnection.h"
#include "receiverpanel.h"
#include "settingsdialog.h"
#include "toolbarwidget.h"
#include "vfopanel.h"
#include "widebandpanel.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    auto *radioMenu = menuBar()->addMenu(tr("&Radio"));

    auto *discoverAction = radioMenu->addAction(tr("&Discover..."));
    connect(discoverAction, &QAction::triggered, this, &MainWindow::showDiscoveryDialog);

    m_disconnectAction = radioMenu->addAction(tr("D&isconnect"));
    m_disconnectAction->setEnabled(false);
    connect(m_disconnectAction, &QAction::triggered, this, &MainWindow::disconnectFromRadio);

    radioMenu->addSeparator();

    m_rx2EnabledAction = radioMenu->addAction(tr("Enable &RX2"));
    m_rx2EnabledAction->setCheckable(true);
    m_rx2EnabledAction->setToolTip(
        tr("A second, independently-tuned receiver sharing the same physical ADC (Protocol 2 only) - "
           "see RadioConnection::setRx2Enabled()."));

    radioMenu->addSeparator();

    auto *exitAction = radioMenu->addAction(tr("E&xit"));
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

    // Gear-menu convention ("Settings"): a single, always-available top-
    // level menu rather than burying it under Radio - see SettingsDialog's
    // class comment for scope/rationale.
    auto *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    auto *settingsAction = settingsMenu->addAction(tr("⚙ Preferences..."));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);

    // Three independently movable/floatable/tabbable docks - see the plan
    // this replaced (a single central QSplitter with one receiver) and
    // ReceiverPanel/WidebandPanel's own class comments. Default layout:
    // Wideband spans the top, RX1/RX2 side by side below it - user-
    // requested via an HTML mockup comparison, see the project's own
    // memory notes.
    m_rx1 = new ReceiverPanel(0, tr("RX1"), this);
    m_rx2 = new ReceiverPanel(1, tr("RX2"), this);
    m_wideband = new WidebandPanel(this);
    m_rx1->setDevicePanel(m_wideband->devicePanel());
    m_rx2->setDevicePanel(m_wideband->devicePanel());

    auto *rx1Dock = new QDockWidget(tr("RX1"), this);
    rx1Dock->setObjectName(QStringLiteral("rx1Dock"));
    rx1Dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                          QDockWidget::DockWidgetClosable);
    rx1Dock->setWidget(m_rx1);

    auto *rx2Dock = new QDockWidget(tr("RX2"), this);
    rx2Dock->setObjectName(QStringLiteral("rx2Dock"));
    rx2Dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                          QDockWidget::DockWidgetClosable);
    rx2Dock->setWidget(m_rx2);
    // RX2 starts disabled - see m_rx2EnabledAction - so no DDC1 traffic or
    // second RxAudioChannel is paid for until the user actually wants it.
    rx2Dock->setVisible(false);

    auto *widebandDock = new QDockWidget(tr("Wideband"), this);
    widebandDock->setObjectName(QStringLiteral("widebandDock"));
    widebandDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                               QDockWidget::DockWidgetClosable);
    widebandDock->setWidget(m_wideband);

    // Analog S-meters - own floating docks, not embedded in VfoPanel (see
    // mainwindow.h's comment on why). Hidden by default; SettingsDialog's
    // Meter tab toggles both together.
    m_rx1MeterWidget = new AnalogMeterWidget(this);
    m_rx1MeterDock = new QDockWidget(tr("RX1 Meter"), this);
    m_rx1MeterDock->setObjectName(QStringLiteral("rx1MeterDock"));
    m_rx1MeterDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                                 QDockWidget::DockWidgetClosable);
    m_rx1MeterDock->setWidget(m_rx1MeterWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_rx1MeterDock);
    m_rx1MeterDock->setFloating(true);
    m_rx1MeterDock->setVisible(false);
    connect(m_rx1, &ReceiverPanel::meterDbmChanged, m_rx1MeterWidget, &AnalogMeterWidget::setDbm);

    m_rx2MeterWidget = new AnalogMeterWidget(this);
    m_rx2MeterDock = new QDockWidget(tr("RX2 Meter"), this);
    m_rx2MeterDock->setObjectName(QStringLiteral("rx2MeterDock"));
    m_rx2MeterDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                                 QDockWidget::DockWidgetClosable);
    m_rx2MeterDock->setWidget(m_rx2MeterWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_rx2MeterDock);
    m_rx2MeterDock->setFloating(true);
    m_rx2MeterDock->setVisible(false);
    connect(m_rx2, &ReceiverPanel::meterDbmChanged, m_rx2MeterWidget, &AnalogMeterWidget::setDbm);

    // AllowNestedDocks is what lets the user drag a floated panel back and
    // drop it beside another one to recreate a side-by-side split - without
    // it, only tabbing (AllowTabbedDocks) works from the GUI, and a
    // once-floated panel can't be re-docked side-by-side at all.
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    // Corner ownership: give the top corners to the Top area so
    // widebandDock spans the full width, and the bottom corners to
    // Left/Right so rx1Dock/rx2Dock fill the remaining space side by side
    // underneath it - more reliable than splitDockWidget() from a single
    // Bottom-area slot, which doesn't consistently honor the requested
    // orientation when the second widget starts hidden (see rx2Dock's
    // setVisible(false) below).
    setCorner(Qt::TopLeftCorner, Qt::TopDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::TopDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    addDockWidget(Qt::TopDockWidgetArea, widebandDock);
    addDockWidget(Qt::LeftDockWidgetArea, rx1Dock);
    addDockWidget(Qt::RightDockWidgetArea, rx2Dock);
    resizeDocks({widebandDock}, {200}, Qt::Vertical);

    connect(m_rx2EnabledAction, &QAction::toggled, this, [this, rx2Dock](bool enabled) {
        rx2Dock->setVisible(enabled);
        if (!m_connection) {
            return;
        }
        QMetaObject::invokeMethod(
            m_connection, [this, enabled]() { m_connection->setRx2Enabled(enabled); }, Qt::QueuedConnection);
        if (enabled) {
            m_rx2->startAudio(m_workerThread, m_rx2->toolbar()->sampleRateHz(), m_connection,
                               /*useSecondDdc=*/true);
            connect(m_rx2, &ReceiverPanel::audioBlockReady, m_audioSink,
                    [this](const QVector<float> &block) {
                        m_rx2Block = block;
                    });
            const double hz = m_rx2->frequencyHz();
            const int rateHz = m_rx2->toolbar()->sampleRateHz();
            QMetaObject::invokeMethod(
                m_connection,
                [this, hz, rateHz]() {
                    m_connection->setRxFrequency2(hz);
                    m_connection->setRxSampleRate2(rateHz);
                },
                Qt::QueuedConnection);
            m_rx2->setConnected(true);
        } else {
            m_rx2->stopAudio();
            m_rx2->setConnected(false);
            m_rx2Block.clear();
        }
    });

    connect(m_rx1, &ReceiverPanel::frequencyChanged, this, [this](double hz) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, hz]() { m_connection->setRxFrequency(hz); }, Qt::QueuedConnection);
        }
    });
    connect(m_rx2, &ReceiverPanel::frequencyChanged, this, [this](double hz) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, hz]() { m_connection->setRxFrequency2(hz); }, Qt::QueuedConnection);
        }
    });
    connect(m_rx1, &ReceiverPanel::sampleRateChanged, this, [this](int hz) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, hz]() { m_connection->setRxSampleRate(hz); }, Qt::QueuedConnection);
        }
    });
    connect(m_rx2, &ReceiverPanel::sampleRateChanged, this, [this](int hz) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, hz]() { m_connection->setRxSampleRate2(hz); }, Qt::QueuedConnection);
        }
    });
    connect(m_rx1, &ReceiverPanel::statusMessage, this, [this](const QString &msg) { statusBar()->showMessage(msg); });
    connect(m_rx2, &ReceiverPanel::statusMessage, this, [this](const QString &msg) { statusBar()->showMessage(msg); });

    DevicePanel *devicePanel = m_wideband->devicePanel();
    connect(devicePanel, &DevicePanel::attenuationChanged, this, [this](int db) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, db]() { m_connection->setAttenuation(db); }, Qt::QueuedConnection);
        }
    });
    connect(devicePanel, &DevicePanel::filterBoardEnabledChanged, this, [this](bool enabled) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, enabled]() { m_connection->setFilterBoardEnabled(enabled); },
                Qt::QueuedConnection);
        }
    });
    connect(devicePanel, &DevicePanel::widebandEnabledChanged, this, [this](bool enabled) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, enabled]() { m_connection->setWidebandEnabled(enabled); },
                Qt::QueuedConnection);
        }
    });

    // Created eagerly (not lazily on first "Preferences..." click) so its
    // checkboxes/spinboxes hold real state - and the signals below are
    // live - even before the user ever opens it, e.g. right after
    // loadSettings() restores a saved value. Parented to `this` but never
    // shown until showSettingsDialog() - see that method's comment.
    m_settingsDialog = new SettingsDialog(this);
    connect(m_settingsDialog, &SettingsDialog::adcDitherChanged, this, [this](bool enabled) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, enabled]() { m_connection->setAdcDither(enabled); }, Qt::QueuedConnection);
        }
    });
    connect(m_settingsDialog, &SettingsDialog::adcRandomChanged, this, [this](bool enabled) {
        if (m_connection) {
            QMetaObject::invokeMethod(
                m_connection, [this, enabled]() { m_connection->setAdcRandom(enabled); }, Qt::QueuedConnection);
        }
    });
    connect(m_settingsDialog, &SettingsDialog::peakHoldChanged, this,
            [this](bool enabled, double holdTimeSec, double dropDbPerSec) {
                for (PanadapterWidget *pan : {m_rx1->panadapter(), m_rx2->panadapter(), m_wideband->panadapter()}) {
                    pan->setPeakHoldParams(holdTimeSec, dropDbPerSec);
                    pan->setPeakHoldEnabled(enabled);
                }
            });
    connect(m_settingsDialog, &SettingsDialog::analogMeterChanged, this, [this](bool enabled) {
        m_rx1MeterDock->setVisible(enabled);
        m_rx2MeterDock->setVisible(enabled);
    });
    connect(m_settingsDialog, &SettingsDialog::s9DbmChanged, this, [this](double dbm) {
        m_rx1->vfoPanel()->setS9Dbm(dbm);
        m_rx2->vfoPanel()->setS9Dbm(dbm);
        m_rx1MeterWidget->setS9Dbm(dbm);
        m_rx2MeterWidget->setS9Dbm(dbm);
    });

    loadSettings();

    m_workerThread = new QThread(this);
    m_workerThread->setObjectName("QhpsdrWorker");
    m_workerThread->start();

    statusBar()->showMessage(tr("Qhpsdr Ready."));
}

MainWindow::~MainWindow() {
    saveSettings();
    disconnectFromRadio();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void MainWindow::saveSettings() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("rx1"));
    m_rx1->saveSettings(settings);
    settings.endGroup();
    settings.beginGroup(QStringLiteral("rx2"));
    m_rx2->saveSettings(settings);
    settings.endGroup();

    DevicePanel *devicePanel = m_wideband->devicePanel();
    settings.beginGroup(QStringLiteral("device"));
    settings.setValue(QStringLiteral("rfGainDb"), devicePanel->rfGainDb());
    settings.setValue(QStringLiteral("attenuationDb"), devicePanel->attenuationDb());
    settings.setValue(QStringLiteral("filterBoardEnabled"), devicePanel->filterBoardEnabled());
    settings.setValue(QStringLiteral("widebandEnabled"), devicePanel->widebandEnabled());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("settings"));
    settings.setValue(QStringLiteral("adcDither"), m_settingsDialog->adcDither());
    settings.setValue(QStringLiteral("adcRandom"), m_settingsDialog->adcRandom());
    settings.setValue(QStringLiteral("peakHoldEnabled"), m_settingsDialog->peakHoldEnabled());
    settings.setValue(QStringLiteral("peakHoldTimeSec"), m_settingsDialog->peakHoldTimeSec());
    settings.setValue(QStringLiteral("peakHoldDropDbPerSec"), m_settingsDialog->peakHoldDropDbPerSec());
    settings.setValue(QStringLiteral("analogMeterEnabled"), m_settingsDialog->analogMeterEnabled());
    settings.setValue(QStringLiteral("s9Dbm"), m_settingsDialog->s9Dbm());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("app"));
    settings.setValue(QStringLiteral("rx2Enabled"), m_rx2EnabledAction->isChecked());
    // Versioned: bumped whenever the default dock layout changes in a way
    // that makes an old saved arrangement stale (e.g. the RX1/RX2 side-by-
    // side fix) - restoreState() below rejects a mismatched version, so a
    // fixed default takes over instead of restoring a broken saved layout.
    settings.setValue(QStringLiteral("windowState"), saveState(/*version=*/1));
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.endGroup();
}

void MainWindow::loadSettings() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("rx1"));
    m_rx1->loadSettings(settings);
    settings.endGroup();
    settings.beginGroup(QStringLiteral("rx2"));
    m_rx2->loadSettings(settings);
    settings.endGroup();

    DevicePanel *devicePanel = m_wideband->devicePanel();
    settings.beginGroup(QStringLiteral("device"));
    devicePanel->setRfGainDb(settings.value(QStringLiteral("rfGainDb"), 0.0).toDouble());
    devicePanel->setAttenuationDb(settings.value(QStringLiteral("attenuationDb"), 0).toInt());
    devicePanel->setFilterBoardEnabled(settings.value(QStringLiteral("filterBoardEnabled"), false).toBool());
    devicePanel->setWidebandEnabled(settings.value(QStringLiteral("widebandEnabled"), false).toBool());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("settings"));
    m_settingsDialog->setAdcDither(settings.value(QStringLiteral("adcDither"), false).toBool());
    m_settingsDialog->setAdcRandom(settings.value(QStringLiteral("adcRandom"), false).toBool());
    m_settingsDialog->setPeakHoldTimeSec(settings.value(QStringLiteral("peakHoldTimeSec"), 2.5).toDouble());
    m_settingsDialog->setPeakHoldDropDbPerSec(
        settings.value(QStringLiteral("peakHoldDropDbPerSec"), 6.0).toDouble());
    m_settingsDialog->setPeakHoldEnabled(settings.value(QStringLiteral("peakHoldEnabled"), false).toBool());
    m_settingsDialog->setAnalogMeterEnabled(settings.value(QStringLiteral("analogMeterEnabled"), false).toBool());
    m_settingsDialog->setS9Dbm(settings.value(QStringLiteral("s9Dbm"), -73.0).toDouble());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("app"));
    m_rx2EnabledAction->setChecked(settings.value(QStringLiteral("rx2Enabled"), false).toBool());
    if (settings.contains(QStringLiteral("windowState"))) {
        restoreState(settings.value(QStringLiteral("windowState")).toByteArray(), /*version=*/1);
    }
    if (settings.contains(QStringLiteral("geometry"))) {
        restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    }
    settings.endGroup();
}

void MainWindow::showSettingsDialog() {
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void MainWindow::showDiscoveryDialog() {
    DiscoveryDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || !dialog.selectedDevice()) {
        return;
    }

    // A real copy, not a reference: selectedDevice() returns
    // std::optional<DiscoveredDevice> by value, so a reference bound
    // through the dereferenced temporary would dangle as soon as this
    // statement ends.
    const DiscoveredDevice device = *dialog.selectedDevice();
    connectToDevice(device);
}

void MainWindow::connectToDevice(const DiscoveredDevice &device) {
    disconnectFromRadio();

    // QAudioSink lives on m_workerThread rather than the GUI thread:
    // write() can block waiting for buffer space, and doing that from a
    // GUI-thread slot stalled the event loop just as badly as the DSP
    // work itself did before that got moved off. Created fresh here
    // (not a QObject subclass constructor) so nothing upstream could
    // accidentally parent it back to `this`.
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), format);
    m_audioSink->moveToThread(m_workerThread);
    QMetaObject::invokeMethod(
        m_audioSink, [this]() { m_audioDevice = m_audioSink->start(); }, Qt::QueuedConnection);

    // Which wire protocol a device speaks only changes which concrete
    // RadioConnection subclass gets constructed here - everything else
    // (signal wiring, thread affinity, per-receiver I/Q feed) is identical
    // since both implement the same RadioConnection interface.
    if (device.protocol == NEW_PROTOCOL) {
        m_connection = new NewProtocolConnection();
    } else {
        m_connection = new OldProtocolConnection();
    }
    m_connection->moveToThread(m_workerThread);
    connect(m_connection, &RadioConnection::statsUpdated, this,
            [this, device](quint64 packets, quint64 samples, double sps) {
                Q_UNUSED(packets);
                Q_UNUSED(samples);
                statusBar()->showMessage(
                    tr("Connected to %1 at %2 - %3 samples/s").arg(device.name, device.address.toString()).arg(sps, 0, 'f', 0));
            });
    connect(m_connection, &RadioConnection::errorOccurred, this, [this](const QString &msg) {
        statusBar()->showMessage(tr("Radio connection error: %1").arg(msg));
    });
    // GUI-thread context (`this`) - Qt auto-queues this across threads,
    // same as statsUpdated/errorOccurred above. Only fires while the
    // shared DevicePanel's widebandEnabled() is on.
    connect(m_connection, &RadioConnection::wideSpectrumReady, this,
            [this](const QVector<float> &magnitudesDb, double sampleRateHz) {
                m_wideband->setSpectrum(magnitudesDb, sampleRateHz);
            });

    m_rx1->startAudio(m_workerThread, m_rx1->toolbar()->sampleRateHz(), m_connection, /*useSecondDdc=*/false);
    connect(m_rx1, &ReceiverPanel::audioBlockReady, m_audioSink, [this](const QVector<float> &block) {
        m_rx1Block = block;
        mixAndPlayAudio();
    });
    const bool rx2Enabled = m_rx2EnabledAction->isChecked();
    if (rx2Enabled) {
        m_rx2->startAudio(m_workerThread, m_rx2->toolbar()->sampleRateHz(), m_connection, /*useSecondDdc=*/true);
        connect(m_rx2, &ReceiverPanel::audioBlockReady, m_audioSink,
                [this](const QVector<float> &block) { m_rx2Block = block; });
    }

    QMetaObject::invokeMethod(
        m_connection, [this, device]() { m_connection->connectToDevice(device, m_rx1->frequencyHz()); },
        Qt::QueuedConnection);
    // A fresh connection object always starts with filterBoardEnabled/
    // widebandEnabled/rx2Enabled=false internally - re-push whatever the
    // shared DevicePanel/menu currently hold (e.g. restored from saved
    // settings) rather than silently resetting them.
    {
        DevicePanel *devicePanel = m_wideband->devicePanel();
        const bool filterBoardEnabled = devicePanel->filterBoardEnabled();
        QMetaObject::invokeMethod(
            m_connection, [this, filterBoardEnabled]() { m_connection->setFilterBoardEnabled(filterBoardEnabled); },
            Qt::QueuedConnection);
        const bool widebandEnabled = devicePanel->widebandEnabled();
        QMetaObject::invokeMethod(
            m_connection, [this, widebandEnabled]() { m_connection->setWidebandEnabled(widebandEnabled); },
            Qt::QueuedConnection);
        const int attenuationDb = devicePanel->attenuationDb();
        QMetaObject::invokeMethod(
            m_connection, [this, attenuationDb]() { m_connection->setAttenuation(attenuationDb); },
            Qt::QueuedConnection);
        const int rx1RateHz = m_rx1->toolbar()->sampleRateHz();
        QMetaObject::invokeMethod(
            m_connection, [this, rx1RateHz]() { m_connection->setRxSampleRate(rx1RateHz); }, Qt::QueuedConnection);
        if (rx2Enabled) {
            const double rx2Hz = m_rx2->frequencyHz();
            const int rx2RateHz = m_rx2->toolbar()->sampleRateHz();
            QMetaObject::invokeMethod(
                m_connection,
                [this, rx2Hz, rx2RateHz]() {
                    m_connection->setRx2Enabled(true);
                    m_connection->setRxFrequency2(rx2Hz);
                    m_connection->setRxSampleRate2(rx2RateHz);
                },
                Qt::QueuedConnection);
        }
    }
    m_rx1->setConnected(true);
    if (rx2Enabled) {
        m_rx2->setConnected(true);
    }
    m_wideband->devicePanel()->setConnected(true);
    m_disconnectAction->setEnabled(true);
}

void MainWindow::mixAndPlayAudio() {
    if (!m_audioDevice) {
        return;
    }
    QVector<float> mixed = m_rx1Block;
    if (!m_rx2Block.isEmpty() && m_rx2Block.size() == mixed.size()) {
        for (int i = 0; i < mixed.size(); ++i) {
            mixed[i] = qBound(-1.0f, mixed[i] + m_rx2Block[i], 1.0f);
        }
    }
    m_audioDevice->write(reinterpret_cast<const char *>(mixed.constData()), mixed.size() * qint64(sizeof(float)));
}

void MainWindow::disconnectFromRadio() {
    if (m_connection) {
        // Both calls run on the worker thread that owns this object;
        // deleteLater() is itself thread-safe to call from anywhere, but
        // disconnectFromDevice() isn't, so it's queued too - in the same
        // invocation, so ordering is preserved.
        RadioConnection *connection = m_connection;
        QMetaObject::invokeMethod(
            connection,
            [connection]() {
                connection->disconnectFromDevice();
                connection->deleteLater();
            },
            Qt::QueuedConnection);
        m_connection = nullptr;
    }
    m_rx1->stopAudio();
    m_rx2->stopAudio();
    if (m_audioSink) {
        QAudioSink *audioSink = m_audioSink;
        QMetaObject::invokeMethod(
            audioSink,
            [audioSink]() {
                audioSink->stop();
                audioSink->deleteLater();
            },
            Qt::QueuedConnection);
        m_audioSink = nullptr;
        m_audioDevice = nullptr;
    }
    m_rx1Block.clear();
    m_rx2Block.clear();
    m_rx1->setConnected(false);
    m_rx2->setConnected(false);
    m_wideband->devicePanel()->setConnected(false);
    m_disconnectAction->setEnabled(false);
    statusBar()->showMessage(tr("Disconnected."));
}
