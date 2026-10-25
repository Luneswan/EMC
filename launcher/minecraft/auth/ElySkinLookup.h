// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include "minecraft/auth/MinecraftAccount.h"

class QNetworkReply;

// Finds a player name on Ely.by and reads its skin. Ely.by gives every name its own signed textures, so an offline
// account can wear the skin of any Ely.by player (its own, once it has uploaded one) without signing in.
class ElySkinLookup : public QObject {
    Q_OBJECT
   public:
    static const QString kApiRoot;

    struct Result {
        enum Kind { Found, NotFound, Failed };
        Kind kind = Failed;
        QString error;
        QString elyName;  // the name as Ely.by spells it
        // the "textures" profile property exactly as Ely.by signed it
        QString texturesValue;
        QString texturesSignature;
        QUrl skinUrl;
        QUrl capeUrl;
        bool slim = false;
        QByteArray metadata;  // the API root's metadata JSON, when asked for
    };

    explicit ElySkinLookup(QObject* parent = nullptr);
    ~ElySkinLookup() override;

    // Looks the name up, replacing any lookup still running. withMetadata also fetches the API metadata (its public
    // key checks the texture signatures in game).
    void start(const QString& name, bool withMetadata = false);
    void abort();

    // Fetches the Ely.by skin an offline account wears and keeps it on the account, so its face shows in the account
    // list and toolbar. Runs in the background; the account is saved when it changes.
    static void refreshOfflineFace(const MinecraftAccountPtr& account);

   signals:
    void finished(const ElySkinLookup::Result& result);

   private:
    QNetworkReply* send(const QString& path, const QByteArray& postJson = {});
    void onNameReply(QNetworkReply* reply);
    void onProfileReply(QNetworkReply* reply);
    void onMetadataReply(QNetworkReply* reply);
    bool failed(QNetworkReply* reply);
    void finish(Result::Kind kind, const QString& error = {});

    QPointer<QNetworkReply> m_reply;
    QString m_name;
    bool m_withMetadata = false;
    Result m_result;
};
