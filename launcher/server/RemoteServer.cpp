// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "RemoteServer.h"

#include <QDateTime>
#include <QDir>
#include <QDnsLookup>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>
#include <QtEndian>

#include "Application.h"
#include "server/ServerList.h"

namespace {
constexpr int kTimeoutMs = 5000;

void writeVarInt(QByteArray& out, int value)
{
    auto v = quint32(value);
    do {
        quint8 byte = v & 0x7F;
        v >>= 7;
        if (v) {
            byte |= 0x80;
        }
        out.append(char(byte));
    } while (v);
}

// Reads a VarInt at pos; returns false while more bytes are needed.
bool readVarInt(const QByteArray& in, int& pos, int& value)
{
    value = 0;
    for (int shift = 0, i = pos; i < in.size() && shift < 35; ++i, shift += 7) {
        const auto byte = quint8(in[i]);
        value |= (byte & 0x7F) << shift;
        if (!(byte & 0x80)) {
            pos = i + 1;
            return true;
        }
    }
    return false;
}

void writeString(QByteArray& out, const QString& text)
{
    const QByteArray utf8 = text.toUtf8();
    writeVarInt(out, int(utf8.size()));
    out.append(utf8);
}

QByteArray packet(const QByteArray& body)
{
    QByteArray out;
    writeVarInt(out, int(body.size()));
    return out + body;
}

// Plain text from a chat component, without § formatting codes.
QString chatText(const QJsonValue& value)
{
    QString text;
    if (value.isString()) {
        text = value.toString();
    } else if (value.isObject()) {
        const auto object = value.toObject();
        text = object.value("text").toString();
        for (const auto& extra : object.value("extra").toArray()) {
            text += chatText(extra);
        }
    } else if (value.isArray()) {
        for (const auto& part : value.toArray()) {
            text += chatText(part);
        }
    }
    static const QRegularExpression codes("§.");
    return text.remove(codes);
}

const QByteArray kRakNetMagic = QByteArray::fromHex("00ffff00fefefefefdfdfdfd12345678");
}  // namespace

RemoteServer::RemoteServer(const QJsonObject& json, QObject* parent) : QObject(parent)
{
    m_id = json.value("id").toString();
    if (m_id.isEmpty()) {
        m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    update(json);
}

void RemoteServer::update(const QJsonObject& json)
{
    m_name = json.value("name").toString();
    m_host = json.value("host").toString().trimmed();
    m_bedrock = json.value("bedrock").toBool();
    m_port = json.value("port").toInt(m_bedrock ? 19132 : 25565);
    m_rconPort = json.value("rconPort").toInt(25575);
    m_rconPassword = json.value("rconPassword").toString();
    m_panelUrl = json.value("panelUrl").toString().trimmed();
    emit changed();
}

QJsonObject RemoteServer::toJson() const
{
    return { { "id", m_id },         { "name", m_name },           { "host", m_host },
             { "port", m_port },     { "bedrock", m_bedrock },     { "rconPort", m_rconPort },
             { "rconPassword", m_rconPassword }, { "panelUrl", m_panelUrl } };
}

QString RemoteServer::address() const
{
    return m_port == (m_bedrock ? 19132 : 25565) ? m_host : QString("%1:%2").arg(m_host).arg(m_port);
}

QString RemoteServer::mcVersion() const
{
    static const QRegularExpression version("\\b(1\\.\\d+(?:\\.\\d+)?)\\b");
    return version.match(m_status.version).captured(1);
}

void RemoteServer::ping()
{
    if (m_pinging || m_host.isEmpty()) {
        return;
    }
    m_pinging = true;
    if (m_bedrock) {
        pingBedrock();
        return;
    }
    // Like the game: a name on the default port may have an SRV record pointing elsewhere.
    if (m_port != 25565 || !QHostAddress(m_host).isNull()) {
        pingJava(m_host, m_port);
        return;
    }
    auto* dns = new QDnsLookup(QDnsLookup::SRV, "_minecraft._tcp." + m_host, this);
    connect(dns, &QDnsLookup::finished, this, [this, dns] {
        dns->deleteLater();
        const auto records = dns->serviceRecords();
        if (dns->error() == QDnsLookup::NoError && !records.isEmpty()) {
            QString target = records.first().target();
            if (target.endsWith('.')) {
                target.chop(1);
            }
            pingJava(target, records.first().port());
        } else {
            pingJava(m_host, m_port);
        }
    });
    dns->lookup();
}

void RemoteServer::pingJava(const QString& host, int port)
{
    auto* socket = new QTcpSocket(this);
    auto* timer = new QTimer(socket);
    timer->setSingleShot(true);
    auto clock = std::make_shared<QElapsedTimer>();
    auto buffer = std::make_shared<QByteArray>();
    auto done = std::make_shared<bool>(false);
    const auto end = [this, socket, done](const Status& status) {
        if (*done) {
            return;
        }
        *done = true;
        socket->abort();
        socket->deleteLater();
        finish(status);
    };
    connect(timer, &QTimer::timeout, this, [end] { end(Status{ true }); });
    connect(socket, &QTcpSocket::errorOccurred, this, [end] { end(Status{ true }); });
    connect(socket, &QTcpSocket::connected, this, [socket, clock, host, port] {
        clock->start();
        QByteArray handshake;
        writeVarInt(handshake, 0x00);
        writeVarInt(handshake, -1);  // any protocol version may ask for the status
        writeString(handshake, host);
        handshake.append(char((port >> 8) & 0xFF)).append(char(port & 0xFF));
        writeVarInt(handshake, 1);  // next: status
        socket->write(packet(handshake));
        socket->write(packet(QByteArray(1, 0x00)));
    });
    connect(socket, &QTcpSocket::readyRead, this, [socket, buffer, clock, end] {
        buffer->append(socket->readAll());
        int pos = 0;
        int length = 0;
        int id = 0;
        int jsonLength = 0;
        if (!readVarInt(*buffer, pos, length) || buffer->size() < pos + length) {
            return;  // wait for the whole packet
        }
        if (!readVarInt(*buffer, pos, id) || id != 0 || !readVarInt(*buffer, pos, jsonLength)) {
            end(Status{ true });
            return;
        }
        const auto json = QJsonDocument::fromJson(buffer->mid(pos, jsonLength)).object();
        Status status;
        status.checked = true;
        status.online = true;
        status.latencyMs = int(clock->elapsed());
        status.motd = chatText(json.value("description")).simplified();
        status.version = chatText(json.value("version").toObject().value("name"));
        const auto players = json.value("players").toObject();
        status.players = players.value("online").toInt();
        status.maxPlayers = players.value("max").toInt();
        for (const auto& player : players.value("sample").toArray()) {
            const QString name = player.toObject().value("name").toString();
            if (!name.isEmpty() && !name.contains(' ')) {  // some servers put adverts here
                status.sample << name;
            }
        }
        end(status);
    });
    timer->start(kTimeoutMs);
    socket->connectToHost(host, quint16(port));
}

void RemoteServer::pingBedrock()
{
    // RakNet "unconnected ping": the reply carries "MCPE;motd;protocol;version;players;max;...".
    auto* socket = new QUdpSocket(this);
    auto* timer = new QTimer(socket);
    timer->setSingleShot(true);
    auto clock = std::make_shared<QElapsedTimer>();
    auto done = std::make_shared<bool>(false);
    const auto end = [this, socket, done](const Status& status) {
        if (*done) {
            return;
        }
        *done = true;
        socket->deleteLater();
        finish(status);
    };
    connect(timer, &QTimer::timeout, this, [end] { end(Status{ true }); });
    connect(socket, &QUdpSocket::readyRead, this, [socket, clock, end] {
        while (socket->hasPendingDatagrams()) {
            QByteArray data(int(socket->pendingDatagramSize()), Qt::Uninitialized);
            socket->readDatagram(data.data(), data.size());
            if (data.size() < 35 || quint8(data[0]) != 0x1C) {
                continue;
            }
            const int length = qFromBigEndian<quint16>(data.constData() + 33);
            const QStringList parts = QString::fromUtf8(data.mid(35, length)).split(';');
            Status status;
            status.checked = true;
            status.online = true;
            status.latencyMs = int(clock->elapsed());
            status.motd = parts.value(1);
            status.version = parts.value(3);
            status.players = parts.value(4).toInt();
            status.maxPlayers = parts.value(5).toInt();
            end(status);
            return;
        }
    });
    QHostAddress address(m_host);
    const auto send = [socket, clock, timer, this](const QHostAddress& target) {
        QByteArray ping;
        ping.append(char(0x01));
        QByteArray time(8, 0);
        qToBigEndian<qint64>(QDateTime::currentMSecsSinceEpoch(), time.data());
        ping.append(time).append(kRakNetMagic);
        QByteArray guid(8, 0);
        qToBigEndian<quint64>(QRandomGenerator::global()->generate64(), guid.data());
        ping.append(guid);
        clock->start();
        socket->writeDatagram(ping, target, quint16(m_port));
        timer->start(kTimeoutMs);
    };
    if (!address.isNull()) {
        send(address);
        return;
    }
    QHostInfo::lookupHost(m_host, this, [send, end](const QHostInfo& info) {
        for (const auto& found : info.addresses()) {
            if (found.protocol() == QAbstractSocket::IPv4Protocol) {
                send(found);
                return;
            }
        }
        end(Status{ true });
    });
}

void RemoteServer::finish(const Status& status)
{
    m_pinging = false;
    m_status = status;
    emit statusChanged();
}

RemoteServerList* RemoteServerList::instance()
{
    static QPointer<RemoteServerList> list;
    if (!list) {
        list = new RemoteServerList(APPLICATION);
    }
    return list;
}

RemoteServerList::RemoteServerList(QObject* parent) : QObject(parent)
{
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    for (const auto& value : QJsonDocument::fromJson(file.readAll()).object().value("servers").toArray()) {
        m_servers << new RemoteServer(value.toObject(), this);
    }
}

QString RemoteServerList::path() const
{
    return QDir(APPLICATION->servers()->root()).filePath("remote.json");
}

RemoteServer* RemoteServerList::add(const QJsonObject& json)
{
    auto* server = new RemoteServer(json, this);
    m_servers << server;
    save();
    emit listChanged();
    server->ping();
    return server;
}

void RemoteServerList::remove(RemoteServer* server)
{
    if (m_servers.removeOne(server)) {
        server->deleteLater();
        save();
        emit listChanged();
    }
}

void RemoteServerList::save() const
{
    QJsonArray array;
    for (auto* server : m_servers) {
        array.append(server->toJson());
    }
    QDir().mkpath(QFileInfo(path()).absolutePath());
    QSaveFile file(path());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{ { "servers", array } }).toJson());
        file.commit();
    }
}

void RemoteServerList::pingAll()
{
    for (auto* server : m_servers) {
        server->ping();
    }
}

RconClient::RconClient(QObject* parent) : QObject(parent) {}

void RconClient::open(const QString& host, int port, const QString& password)
{
    close();
    m_password = password;
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, [this] { write(1, 3, m_password.toUtf8()); });  // 3: log in
    connect(m_socket, &QTcpSocket::readyRead, this, &RconClient::onData);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] {
        const QString reason = m_socket->errorString();
        const bool wasReady = m_ready;
        m_ready = false;
        emit failed(m_socket->error() == QAbstractSocket::RemoteHostClosedError && !wasReady
                        ? tr("The server closed the connection. Check that RCON is on (enable-rcon=true) and the port.")
                        : reason);
    });
    m_socket->connectToHost(host, quint16(port));
}

void RconClient::close()
{
    m_ready = false;
    m_buffer.clear();
    m_pending.clear();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void RconClient::send(const QString& command)
{
    if (!m_ready) {
        return;
    }
    const int id = m_nextId++;
    m_pending.insert(id, command);
    write(id, 2, command.toUtf8());  // 2: run a command
}

void RconClient::write(int id, int type, const QByteArray& body)
{
    QByteArray data(12, 0);
    qToLittleEndian<qint32>(qint32(body.size() + 10), data.data());
    qToLittleEndian<qint32>(id, data.data() + 4);
    qToLittleEndian<qint32>(type, data.data() + 8);
    data.append(body).append(2, '\0');
    m_socket->write(data);
}

void RconClient::onData()
{
    m_buffer.append(m_socket->readAll());
    while (m_buffer.size() >= 4) {
        const int length = qFromLittleEndian<qint32>(m_buffer.constData());
        if (length < 10 || length > 1 << 20) {
            emit failed(tr("The server sent something that is not RCON."));
            close();
            return;
        }
        if (m_buffer.size() < 4 + length) {
            return;
        }
        const int id = qFromLittleEndian<qint32>(m_buffer.constData() + 4);
        const int type = qFromLittleEndian<qint32>(m_buffer.constData() + 8);
        const QString body = QString::fromUtf8(m_buffer.mid(12, length - 10));
        m_buffer.remove(0, 4 + length);
        if (!m_ready && type == 2) {  // the answer to logging in
            if (id == -1) {
                emit failed(tr("The RCON password is wrong."));
                close();
                return;
            }
            m_ready = true;
            emit ready();
        } else if (type == 0) {
            static const QRegularExpression codes("§.");
            emit output(m_pending.take(id), QString(body).remove(codes));
        }
    }
}
