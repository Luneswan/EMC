// SPDX-License-Identifier: GPL-3.0-only
#include "OfflineSkinServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr int kMaxRequest = 64 * 1024;

QString compactId(QString id)
{
    return id.remove('-').toLower();
}
}  // namespace

OfflineSkinServer::OfflineSkinServer(const QString& uuid,
                                     const QString& name,
                                     const QString& texturesValue,
                                     const QString& texturesSignature,
                                     const QByteArray& elyMetadata,
                                     QObject* parent)
    : QTcpServer(parent)
    , m_uuid(compactId(uuid))
    , m_name(name)
    , m_texturesValue(texturesValue)
    , m_texturesSignature(texturesSignature)
{
    // Ely.by's own metadata keeps its public key and skin domains; only the name shown in game changes.
    auto metadata = QJsonDocument::fromJson(elyMetadata).object();
    auto meta = metadata.value("meta").toObject();
    meta["serverName"] = "Ely.by skins (EMC)";
    meta["implementationName"] = "EMC";
    metadata["meta"] = meta;
    m_metadata = QJsonDocument(metadata).toJson(QJsonDocument::Compact);

    connect(this, &QTcpServer::newConnection, this, [this] {
        while (auto* socket = nextPendingConnection()) {
            m_buffers.insert(socket, {});
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                m_buffers.remove(socket);
                socket->deleteLater();
            });
        }
    });
}

bool OfflineSkinServer::start()
{
    return listen(QHostAddress::LocalHost, 0);
}

QString OfflineSkinServer::apiRoot() const
{
    return QString("http://127.0.0.1:%1").arg(serverPort());
}

QString OfflineSkinServer::metadataBase64() const
{
    return QString::fromLatin1(m_metadata.toBase64());
}

void OfflineSkinServer::onReadyRead(QTcpSocket* socket)
{
    if (socket->property("answered").toBool()) {
        socket->readAll();
        return;
    }
    auto& buffer = m_buffers[socket];
    buffer += socket->readAll();
    if (buffer.size() > kMaxRequest) {
        reply(socket, 413);
        return;
    }
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;

    const auto lines = buffer.left(headerEnd).split('\n');
    const auto requestLine = lines.value(0).trimmed().split(' ');
    qsizetype contentLength = 0;
    for (const auto& line : lines) {
        const int colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == "content-length")
            contentLength = line.mid(colon + 1).trimmed().toLongLong();
    }
    if (buffer.size() < headerEnd + 4 + contentLength)
        return;  // the body is still arriving
    if (requestLine.size() < 2) {
        reply(socket, 400);
        return;
    }
    respond(socket, requestLine[0], QString::fromUtf8(requestLine[1]), buffer.mid(headerEnd + 4, contentLength));
}

void OfflineSkinServer::respond(QTcpSocket* socket, const QByteArray& method, const QString& target, const QByteArray& body)
{
    const QUrl url(target);
    const QString path = url.path();
    const QString profilePrefix = "/sessionserver/session/minecraft/profile/";
    const QString namePrefix = "/api/users/profiles/minecraft/";
    qDebug() << "Offline skin server:" << method << path;

    if (method == "GET" && (path == "/" || path.isEmpty())) {
        reply(socket, 200, m_metadata);
    } else if (method == "GET" && path.startsWith(profilePrefix)) {
        if (isOurId(path.mid(profilePrefix.size())))
            reply(socket, 200, profileJson(QUrlQuery(url).queryItemValue("unsigned") != "true"));
        else
            reply(socket, 204);
    } else if (method == "POST" && path == "/api/profiles/minecraft") {
        QJsonArray found;
        for (const auto& name : QJsonDocument::fromJson(body).array())
            if (name.toString().compare(m_name, Qt::CaseInsensitive) == 0)
                found.append(QJsonDocument::fromJson(nameJson()).object());
        reply(socket, 200, QJsonDocument(found).toJson(QJsonDocument::Compact));
    } else if (method == "GET" && path.startsWith(namePrefix)) {
        if (QUrl::fromPercentEncoding(path.mid(namePrefix.size()).toUtf8()).compare(m_name, Qt::CaseInsensitive) == 0)
            reply(socket, 200, nameJson());
        else
            reply(socket, 204);
    } else if (path == "/sessionserver/session/minecraft/join" || path == "/sessionserver/session/minecraft/hasJoined") {
        // Offline players only join offline-mode servers, which never ask the session server.
        reply(socket, 204);
    } else {
        // Account services (chat reports, friends, profile keys) do not exist for offline players, as without EMC.
        reply(socket, 404, R"({"error":"Not Found"})");
    }
}

void OfflineSkinServer::reply(QTcpSocket* socket, int status, const QByteArray& json)
{
    const char* reason = status == 200 ? "OK" : status == 204 ? "No Content" : status == 400 ? "Bad Request" : status == 413 ? "Payload Too Large" : "Not Found";
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    if (status != 204) {
        response += "Content-Type: application/json; charset=utf-8\r\n";
        response += "Content-Length: " + QByteArray::number(json.size()) + "\r\n";
    }
    response += "Connection: close\r\n\r\n";
    if (status != 204)
        response += json;
    socket->setProperty("answered", true);
    socket->write(response);
    socket->disconnectFromHost();
}

QByteArray OfflineSkinServer::profileJson(bool withSignature) const
{
    QJsonObject textures{ { "name", "textures" }, { "value", m_texturesValue } };
    if (withSignature)
        textures["signature"] = m_texturesSignature;
    QJsonObject profile{ { "id", m_uuid }, { "name", m_name }, { "properties", QJsonArray{ textures } } };
    return QJsonDocument(profile).toJson(QJsonDocument::Compact);
}

QByteArray OfflineSkinServer::nameJson() const
{
    return QJsonDocument(QJsonObject{ { "id", m_uuid }, { "name", m_name } }).toJson(QJsonDocument::Compact);
}

bool OfflineSkinServer::isOurId(QString id) const
{
    return compactId(id) == m_uuid;
}
