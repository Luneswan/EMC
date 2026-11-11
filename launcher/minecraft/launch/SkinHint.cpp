// SPDX-License-Identifier: GPL-3.0-only
#include "SkinHint.h"

#include <QDataStream>
#include <QDnsLookup>
#include <QHostAddress>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>

#include <io/stream_reader.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_string.h>
#include <sstream>

#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/launch/MinecraftTarget.h"

namespace {

constexpr int kMaxServers = 16;
constexpr int kTimeoutMs = 8000;
// Any recent protocol number: a status ping is answered whatever the version.
constexpr int kProtocol = 774;

void writeVarInt(QByteArray& data, quint32 value)
{
    while (value & ~0x7Fu) {
        data.append(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    data.append(static_cast<char>(value));
}

void writeString(QByteArray& data, const QByteArray& utf8)
{
    writeVarInt(data, static_cast<quint32>(utf8.size()));
    data.append(utf8);
}

QByteArray framed(const QByteArray& body)
{
    QByteArray out;
    writeVarInt(out, static_cast<quint32>(body.size()));
    out.append(body);
    return out;
}

// The "ip" of every server in the instance's servers.dat, in list order.
QStringList savedServers(MinecraftInstance* instance)
{
    QStringList out;
    if (!instance)
        return out;
    try {
        auto res = FS::read(FS::PathCombine(instance->gameRoot(), "servers.dat"));
        if (!res)
            return out;
        const auto& input = res.value();
        std::istringstream stream(std::string(input.constData(), input.size()));
        auto pair = nbt::io::read_compound(stream);
        if (!pair.second || !pair.second->has_key("servers", nbt::tag_type::List))
            return out;
        for (auto& entry : pair.second->at("servers").as<nbt::tag_list>()) {
            auto& server = entry.as<nbt::tag_compound>();
            if (!server.has_key("ip", nbt::tag_type::String))
                continue;
            std::string ip(server["ip"]);
            out << QString::fromUtf8(ip.c_str()).trimmed();
        }
    } catch (...) {
        // An unreadable list is no reason to stop a launch.
    }
    return out;
}

}  // namespace

void SkinHint::sendAll(MinecraftInstance* instance, const QString& player, const QString& elyName, const QString& joinAddress)
{
    if (player.isEmpty() || elyName.isEmpty())
        return;
    QStringList addresses = savedServers(instance);
    if (!joinAddress.isEmpty())
        addresses.prepend(joinAddress);
    QSet<QString> seen;
    int sent = 0;
    for (const auto& address : addresses) {
        const QString key = address.toLower();
        if (address.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        if (sent++ >= kMaxServers)
            break;
        (new SkinHint(address, player, elyName))->start();
    }
}

SkinHint::SkinHint(const QString& address, const QString& player, const QString& elyName)
    : QObject(nullptr), m_address(address), m_player(player), m_ely(elyName)
{}

void SkinHint::start()
{
    QTimer::singleShot(kTimeoutMs, this, &SkinHint::finish);
    const auto target = MinecraftTarget::parse(m_address, false);
    if (target.address.isEmpty()) {
        finish();
        return;
    }
    // Like the game: a name on the default port may point elsewhere through an SRV record. The hint then names the
    // SRV target, which is what tunnels such as playit.gg expect to see in the handshake.
    if (target.port == 25565 && QHostAddress(target.address).isNull()) {
        auto* lookup = new QDnsLookup(QDnsLookup::SRV, QString("_minecraft._tcp.%1").arg(target.address), this);
        connect(lookup, &QDnsLookup::finished, this, [this, lookup, target] {
            lookup->deleteLater();
            const auto records = lookup->serviceRecords();
            if (lookup->error() == QDnsLookup::NoError && !records.isEmpty()) {
                QString host = records.first().target();
                if (host.endsWith('.'))
                    host.chop(1);
                connectTo(host, records.first().port(), host);
            } else {
                connectTo(target.address, target.port, target.address);
            }
        });
        lookup->lookup();
        return;
    }
    connectTo(target.address, target.port, target.address);
}

void SkinHint::connectTo(const QString& host, quint16 port, const QString& handshakeHost)
{
    if (m_done)
        return;
    const QChar nul(0);
    const QByteArray hostField = (handshakeHost + nul + "EMC-SKIN" + nul + m_player + nul + m_ely).toUtf8();
    if (hostField.size() > 255) {  // the protocol's limit for the address
        finish();
        return;
    }
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, [this, hostField, port] {
        QByteArray handshake;
        writeVarInt(handshake, 0x00);
        writeVarInt(handshake, kProtocol);
        writeString(handshake, hostField);
        handshake.append(static_cast<char>((port >> 8) & 0xFF));
        handshake.append(static_cast<char>(port & 0xFF));
        writeVarInt(handshake, 0x01);  // next state: status
        QByteArray status;
        writeVarInt(status, 0x00);
        m_socket->write(framed(handshake) + framed(status));
        m_socket->flush();
    });
    // The server's answer (or its closing the connection) means the hint arrived; nothing in it is needed.
    connect(m_socket, &QTcpSocket::readyRead, this, &SkinHint::finish);
    connect(m_socket, &QTcpSocket::disconnected, this, &SkinHint::finish);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &SkinHint::finish);
    m_socket->connectToHost(host, port);
}

void SkinHint::finish()
{
    if (m_done)
        return;
    m_done = true;
    if (m_socket)
        m_socket->abort();
    deleteLater();
}
