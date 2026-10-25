#pragma once
#include <QWidget>
#include <QProcess>
#include <QTimer>
#include <functional>

class QLineEdit;
class QSpinBox;
class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;

class WirelessPanel : public QWidget
{
    Q_OBJECT
public:
    explicit WirelessPanel(QWidget *parent = nullptr);
    ~WirelessPanel() override;
signals:
    void devicesChanged();
private:
    void scan();
    void pair();
    void connectPhone(const QString &endpoint);
    void run(const QStringList &args, std::function<void(bool, const QString &)> done,
             const QByteArray &input = {});
    void finish(bool success, const QString &output);
    void remember();
    QLineEdit *m_host;
    QSpinBox *m_pairPort;
    QSpinBox *m_connectPort;
    QLineEdit *m_code;
    QCheckBox *m_auto;
    QLabel *m_status;
    QListWidget *m_services;
    QPushButton *m_pairButton;
    QPushButton *m_connectButton;
    QProcess m_process;
    QTimer m_poll;
    QTimer m_timeout;
    QByteArray m_input;
    std::function<void(bool, const QString &)> m_done;
    QString m_pendingHost;
    int m_discoveryAttempts = 0;
};
