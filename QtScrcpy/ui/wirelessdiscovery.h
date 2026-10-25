#pragma once

#include <QHostAddress>
#include <QRegularExpression>
#include <QStringList>

namespace WirelessDiscovery {
inline QString endpoint(const QString &host, int port)
{
    const QString input = host.trimmed();
    if (!input.contains(':') && !QRegularExpression("^(?:[0-9]{1,3}\\.){3}[0-9]{1,3}$").match(input).hasMatch()) { return {}; }
    QHostAddress address;
    if (!address.setAddress(host.trimmed()) || port < 1 || port > 65535
            || address.isNull() || address.isMulticast() || address == QHostAddress::Any
            || address == QHostAddress::AnyIPv6 || address == QHostAddress::Broadcast) {
        return {};
    }
    return address.protocol() == QAbstractSocket::IPv6Protocol
            ? QString("[%1]:%2").arg(address.toString()).arg(port)
            : QString("%1:%2").arg(address.toString()).arg(port);
}

inline QStringList connectionServices(const QString &output)
{
    QStringList result;
    const QRegularExpression service("_adb-tls-connect\\._tcp\\.?\\s+(\\S+)");
    auto matches = service.globalMatch(output);
    while (matches.hasNext()) {
        const auto value = matches.next().captured(1);
        const int colon = value.lastIndexOf(':');
        QString host = value.left(colon);
        if (host.startsWith('[') && host.endsWith(']')) {
            host = host.mid(1, host.size() - 2);
        }
        const QString valid = endpoint(host, value.mid(colon + 1).toInt());
        if (!valid.isEmpty() && !result.contains(valid)) {
            result.append(valid);
        }
    }
    return result;
}
}
