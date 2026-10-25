#include "wirelesspanel.h"
#include "wirelessdiscovery.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QProcessEnvironment>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

WirelessPanel::WirelessPanel(const QString &adbPath, QWidget *parent, int timeoutMs)
    : QWidget(parent), m_adbPath(adbPath), m_timeoutMs(qBound(100, timeoutMs, 30000))
{
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(tr("Enable Wireless debugging on your phone. Open Pair device with pairing code. "
                              "Pairing and connection ports are different; connection discovery runs after pairing."));
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *form = new QFormLayout;
    m_host = new QLineEdit;
    m_host->setObjectName("phoneIp");
    m_host->setPlaceholderText("192.168.1.35");
    m_pairPort = new QSpinBox;
    m_pairPort->setObjectName("pairPort");
    m_pairPort->setRange(1, 65535);
    m_connectPort = new QSpinBox;
    m_connectPort->setObjectName("connectPort");
    m_connectPort->setRange(0, 65535);
    m_connectPort->setSpecialValueText(tr("Discover automatically"));
    m_code = new QLineEdit;
    m_code->setObjectName("pairCode");
    m_code->setMaxLength(6);
    m_code->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Phone IP address"), m_host);
    form->addRow(tr("Pairing port"), m_pairPort);
    form->addRow(tr("Pairing code"), m_code);
    form->addRow(tr("Connection port (optional)"), m_connectPort);
    layout->addLayout(form);
    auto *buttons = new QHBoxLayout;
    m_pairButton = new QPushButton(tr("Pair and connect"));
    m_pairButton->setObjectName("pairPhone");
    m_connectButton = new QPushButton(tr("Connect paired phone"));
    m_connectButton->setObjectName("connectPhone");
    buttons->addWidget(m_pairButton);
    buttons->addWidget(m_connectButton);
    layout->addLayout(buttons);
    m_auto = new QCheckBox(tr("Automatically discover wireless phones"));
    m_auto->setObjectName("autoDiscovery");
    layout->addWidget(m_auto);
    m_services = new QListWidget;
    m_services->setToolTip(tr("Double-click a discovered phone to connect. Pair it first if necessary."));
    layout->addWidget(m_services);
    m_status = new QLabel(tr("Ready. Pairing codes are never saved."));
    m_status->setObjectName("connectionStatus");
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    QSettings settings;
    m_host->setText(settings.value("wireless/host").toString());
    m_pairPort->setValue(settings.value("wireless/pairPort", 37000).toInt());
    m_connectPort->setValue(settings.value("wireless/connectPort", 0).toInt());
    m_auto->setChecked(settings.value("wireless/discover", true).toBool());
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() { m_process.kill(); });
    connect(&m_process, &QProcess::started, this, [this]() {
        if (!m_input.isEmpty()) {
            m_process.write(m_input);
            m_process.closeWriteChannel();
            m_input.fill('\0');
            m_input.clear();
        }
    });
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(m_process.readAllStandardOutput())
                + QString::fromUtf8(m_process.readAllStandardError());
        finish(status == QProcess::NormalExit && code == 0, output);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finish(false, tr("ADB could not start. Set a valid ADB path in Advanced Config."));
        }
    });
    connect(m_pairButton, &QPushButton::clicked, this, &WirelessPanel::pair);
    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
        if (WirelessDiscovery::endpoint(m_host->text(), 1).isEmpty()) {
            m_status->setText(tr("Enter a complete phone IP address."));
            return;
        }
        remember();
        if (m_connectPort->value()) {
            connectPhone(WirelessDiscovery::endpoint(m_host->text(), m_connectPort->value()));
        } else {
            m_pendingHost = QHostAddress(m_host->text().trimmed()).toString();
            m_discoveryAttempts = 0;
            scan();
        }
    });
    connect(m_services, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        connectPhone(item->text());
    });
    connect(m_auto, &QCheckBox::toggled, this, [this]() { remember(); scan(); });
    m_poll.setInterval(5000);
    connect(&m_poll, &QTimer::timeout, this, &WirelessPanel::scan);
    m_poll.start();
    QTimer::singleShot(0, this, &WirelessPanel::scan);
}

WirelessPanel::~WirelessPanel()
{
    m_done = {};
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

void WirelessPanel::remember()
{
    QSettings settings;
    settings.setValue("wireless/host", m_host->text().trimmed());
    settings.setValue("wireless/pairPort", m_pairPort->value());
    settings.setValue("wireless/connectPort", m_connectPort->value());
    settings.setValue("wireless/discover", m_auto->isChecked());
}

void WirelessPanel::run(const QStringList &args, std::function<void(bool, const QString &)> done,
                        const QByteArray &input)
{
    if (m_done || m_process.state() != QProcess::NotRunning) {
        return;
    }
    QString program;
    const QStringList candidates = {m_adbPath,
        QString::fromLocal8Bit(qgetenv("QTSCRCPY_ADB_PATH")),
        QCoreApplication::applicationDirPath() + "/adb.exe",
        QCoreApplication::applicationDirPath() + "/adb", QStandardPaths::findExecutable("adb")};
    for (const auto &candidate : candidates) {
        if (QFileInfo(candidate).isFile()) {
            program = candidate;
            break;
        }
    }
    m_pairButton->setEnabled(false);
    m_connectButton->setEnabled(false);
    m_done = std::move(done);
    m_input = input;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("ADB_MDNS_OPENSCREEN", "1");
    m_process.setProcessEnvironment(environment);
    m_timeout.start(m_timeoutMs);
    m_process.start(program, args);
}

void WirelessPanel::finish(bool success, const QString &output)
{
    m_timeout.stop();
    m_input.fill('\0');
    m_input.clear();
    m_pairButton->setEnabled(true);
    m_connectButton->setEnabled(true);
    auto done = std::move(m_done);
    m_done = {};
    if (done) {
        done(success, output);
    }
}

void WirelessPanel::pair()
{
    const auto address = WirelessDiscovery::endpoint(m_host->text(), m_pairPort->value());
    if (address.isEmpty() || !QRegularExpression("^[0-9]{6}$").match(m_code->text()).hasMatch()) {
        m_status->setText(tr("Enter a complete IP address, pairing port and six-digit pairing code."));
        return;
    }
    remember();
    const QString host = QHostAddress(m_host->text().trimmed()).toString();
    const QString connection = m_connectPort->value()
            ? WirelessDiscovery::endpoint(host, m_connectPort->value()) : QString();
    m_status->setText(tr("Pairing with phone…"));
    const QByteArray code = m_code->text().toUtf8() + '\n';
    m_code->clear();
    // Send the one-time code on stdin, not in a process command line or log.
    run({"pair", address}, [this, host, connection](bool ok, const QString &output) {
        if (!ok || !output.contains("Successfully paired", Qt::CaseInsensitive)) {
            m_status->setText(tr("Pairing failed or timed out. Reopen the phone's pairing dialog and check its current port and code."));
            return;
        }
        m_status->setText(tr("Paired. Discovering connection port…"));
        m_pendingHost = host;
        m_discoveryAttempts = 0;
        if (connection.isEmpty()) {
            scan();
        } else {
            connectPhone(connection);
        }
    }, code);
}

void WirelessPanel::scan()
{
    if ((!m_auto->isChecked() && m_pendingHost.isEmpty()) || m_done) {
        return;
    }
    run({"mdns", "services"}, [this](bool ok, const QString &output) {
        if (!ok) {
            m_status->setText(tr("Discovery unavailable. Enter the connection port from Wireless debugging on your phone."));
            return;
        }
        const auto services = WirelessDiscovery::connectionServices(output);
        const QString selected = m_services->currentItem() ? m_services->currentItem()->text() : QString();
        m_services->clear();
        m_services->addItems(services);
        for (int i = 0; i < m_services->count(); ++i) {
            if (m_services->item(i)->text() == selected) {
                m_services->setCurrentRow(i);
            }
        }
        emit devicesChanged();
        if (!m_pendingHost.isEmpty()) {
            for (const auto &service : services) {
                if (service == WirelessDiscovery::endpoint(m_pendingHost, service.mid(service.lastIndexOf(':') + 1).toInt())) {
                    connectPhone(service);
                    return;
                }
            }
            if (++m_discoveryAttempts >= 6) {
                m_pendingHost.clear();
                m_status->setText(tr("Paired phone not discovered. Enter its connection port from the main Wireless debugging screen, then click Connect paired phone."));
            }
        }
    });
}

void WirelessPanel::connectPhone(const QString &address)
{
    if (address.isEmpty() || m_done) {
        return;
    }
    m_pendingHost.clear();
    m_status->setText(tr("Connecting to %1…").arg(address));
    run({"connect", address}, [this](bool ok, const QString &output) {
        if (ok && (output.contains("connected to") || output.contains("already connected"))) {
            m_status->setText(tr("Connected. Select your phone in the device list and click Start."));
            emit devicesChanged();
        } else {
            m_status->setText(tr("Connection failed. Check the current connection port, Wi-Fi network and phone authorization."));
        }
    });
}
