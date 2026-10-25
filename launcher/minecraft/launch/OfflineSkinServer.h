// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QHash>
#include <QTcpServer>

class QTcpSocket;

// A tiny authlib-injector API on 127.0.0.1 for one offline player. It answers the game's profile lookups for the
// offline UUID with the textures Ely.by signed for the chosen name, and hands out Ely.by's public key so the game
// accepts that signature. The UUID stays the offline one, so worlds, inventories and server data are untouched.
class OfflineSkinServer : public QTcpServer {
    Q_OBJECT
   public:
    OfflineSkinServer(const QString& uuid,
                      const QString& name,
                      const QString& texturesValue,
                      const QString& texturesSignature,
                      const QByteArray& elyMetadata,
                      QObject* parent = nullptr);

    bool start();
    QString apiRoot() const;
    // The metadata the game sees, base64 for -Dauthlibinjector.yggdrasil.prefetched
    QString metadataBase64() const;

   private:
    void onReadyRead(QTcpSocket* socket);
    void respond(QTcpSocket* socket, const QByteArray& method, const QString& path, const QByteArray& body);
    static void reply(QTcpSocket* socket, int status, const QByteArray& json = {});
    QByteArray profileJson(bool withSignature) const;
    QByteArray nameJson() const;
    bool isOurId(QString id) const;

    QString m_uuid;
    QString m_name;
    QString m_texturesValue;
    QString m_texturesSignature;
    QByteArray m_metadata;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};
