// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerNetwork.h"

#include <QNetworkInterface>
#include <QTcpServer>
#include <QUdpSocket>

#ifdef Q_OS_WIN
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
// clang-format on
#endif

QString ServerNetwork::lanAddress()
{
    // Skip virtual adapters (VPNs, WSL, VMs): friends cannot reach those addresses.
    static const QStringList virtualNames{ "virtual", "vethernet", "vmware", "wsl", "hyper-v", "loopback", "tailscale",
                                           "zerotier", "hamachi", "radmin", "vpn", "tap", "tun" };
    QString fallback;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        const QString name = (iface.humanReadableName() + ' ' + iface.name()).toLower();
        const bool isVirtual = std::any_of(virtualNames.begin(), virtualNames.end(), [&](const QString& v) { return name.contains(v); });
        for (const auto& entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || !ip.isPrivateUse()) {
                continue;
            }
            if (!isVirtual && (iface.type() == QNetworkInterface::Wifi || iface.type() == QNetworkInterface::Ethernet)) {
                return ip.toString();
            }
            if (fallback.isEmpty() && !isVirtual) {
                fallback = ip.toString();
            }
        }
    }
    return fallback;
}

#ifdef Q_OS_WIN
namespace {
// Every local port some program is listening on (TCP) or bound to (UDP), on any address. Reading
// the system tables opens no socket, so Windows Firewall never asks about EMC itself.
QSet<int> boundPorts(bool udp)
{
    QSet<int> ports;
    for (const ULONG family : { ULONG(AF_INET), ULONG(AF_INET6) }) {
        DWORD size = 0;
        const auto query = [&](void* buffer) {
            return udp ? GetExtendedUdpTable(buffer, &size, FALSE, family, UDP_TABLE_OWNER_PID, 0)
                       : GetExtendedTcpTable(buffer, &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        };
        if (query(nullptr) != ERROR_INSUFFICIENT_BUFFER) {
            continue;
        }
        QByteArray buffer(int(size) + 1024, Qt::Uninitialized);
        size = DWORD(buffer.size());
        if (query(buffer.data()) != NO_ERROR) {
            continue;
        }
        const auto add = [&](const auto* table) {
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                ports.insert(ntohs(u_short(table->table[i].dwLocalPort)));
            }
        };
        if (udp && family == AF_INET) {
            add(reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(buffer.constData()));
        } else if (udp) {
            add(reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(buffer.constData()));
        } else if (family == AF_INET) {
            add(reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.constData()));
        } else {
            add(reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.constData()));
        }
    }
    return ports;
}
}  // namespace
#endif

bool ServerNetwork::portAvailable(int port, bool udp)
{
#ifdef Q_OS_WIN
    return !boundPorts(udp).contains(port);
#else
    // Probe loopback only: a listening socket on a public address would make some firewalls ask
    // about EMC. Without SO_REUSEADDR this still fails when another program holds *:port.
    const QHostAddress address(QHostAddress::LocalHost);
    if (udp) {
        QUdpSocket socket;
        return socket.bind(address, quint16(port), QAbstractSocket::DontShareAddress);
    }
    QTcpServer server;
    return server.listen(address, quint16(port));
#endif
}

int ServerNetwork::nextAvailablePort(int start, bool udp, const QSet<int>& taken)
{
    for (int port = start; port < start + 200 && port <= 65535; ++port) {
        if (!taken.contains(port) && portAvailable(port, udp)) {
            return port;
        }
    }
    return start;
}
