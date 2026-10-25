// SPDX-License-Identifier: GPL-3.0-only
#include "ElySkinLookup.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "Application.h"

const QString ElySkinLookup::kApiRoot = QStringLiteral("https://authserver.ely.by/api/authlib-injector");

namespace {
// Ely.by stores textures behind plain http links; the same files are served over https.
QUrl secureUrl(const QString& url)
{
    QUrl result(url);
    if (result.scheme() == "http" && result.host().endsWith("ely.by"))
        result.setScheme("https");
    return result;
}
}  // namespace

ElySkinLookup::ElySkinLookup(QObject* parent) : QObject(parent) {}

ElySkinLookup::~ElySkinLookup()
{
    abort();
}

void ElySkinLookup::start(const QString& name, bool withMetadata)
{
    abort();
    m_name = name.trimmed();
    m_withMetadata = withMetadata;
    m_result = Result();
    if (m_name.isEmpty()) {
        finish(Result::NotFound);
        return;
    }
    auto* reply = send("/api/profiles/minecraft", QJsonDocument(QJsonArray{ m_name }).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onNameReply(reply); });
}

void ElySkinLookup::abort()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    m_reply = nullptr;
}

QNetworkReply* ElySkinLookup::send(const QString& path, const QByteArray& postJson)
{
    QNetworkRequest request(QUrl(kApiRoot + path));
    request.setTransferTimeout(10000);
    request.setRawHeader("Accept", "application/json");
    if (postJson.isEmpty()) {
        m_reply = APPLICATION->network()->get(request);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        m_reply = APPLICATION->network()->post(request, postJson);
    }
    return m_reply;
}

bool ElySkinLookup::failed(QNetworkReply* reply)
{
    reply->deleteLater();
    m_reply = nullptr;
    if (reply->error() == QNetworkReply::NoError)
        return false;
    finish(Result::Failed, reply->errorString());
    return true;
}

void ElySkinLookup::onNameReply(QNetworkReply* reply)
{
    if (failed(reply))
        return;
    QString id;
    for (const auto& entry : QJsonDocument::fromJson(reply->readAll()).array()) {
        const auto profile = entry.toObject();
        if (profile.value("name").toString().compare(m_name, Qt::CaseInsensitive) == 0) {
            id = profile.value("id").toString();
            m_result.elyName = profile.value("name").toString();
        }
    }
    if (id.isEmpty()) {
        finish(Result::NotFound);
        return;
    }
    auto* next = send("/sessionserver/session/minecraft/profile/" + id + "?unsigned=false");
    connect(next, &QNetworkReply::finished, this, [this, next] { onProfileReply(next); });
}

void ElySkinLookup::onProfileReply(QNetworkReply* reply)
{
    if (failed(reply))
        return;
    for (const auto& entry : QJsonDocument::fromJson(reply->readAll()).object().value("properties").toArray()) {
        const auto property = entry.toObject();
        if (property.value("name").toString() == "textures") {
            m_result.texturesValue = property.value("value").toString();
            m_result.texturesSignature = property.value("signature").toString();
        }
    }
    const auto textures = QJsonDocument::fromJson(QByteArray::fromBase64(m_result.texturesValue.toLatin1())).object().value("textures").toObject();
    const auto skin = textures.value("SKIN").toObject();
    m_result.skinUrl = secureUrl(skin.value("url").toString());
    m_result.slim = skin.value("metadata").toObject().value("model").toString() == "slim";
    m_result.capeUrl = secureUrl(textures.value("CAPE").toObject().value("url").toString());
    // A name without a skin, or without a signature the game can check, gives the default skin anyway.
    if (!m_result.skinUrl.isValid() || m_result.skinUrl.isEmpty() || m_result.texturesSignature.isEmpty()) {
        finish(Result::NotFound);
        return;
    }
    if (!m_withMetadata) {
        finish(Result::Found);
        return;
    }
    auto* next = send(QString());
    connect(next, &QNetworkReply::finished, this, [this, next] { onMetadataReply(next); });
}

void ElySkinLookup::onMetadataReply(QNetworkReply* reply)
{
    if (failed(reply))
        return;
    m_result.metadata = reply->readAll();
    if (QJsonDocument::fromJson(m_result.metadata).object().value("signaturePublickey").toString().isEmpty()) {
        finish(Result::Failed, tr("Ely.by did not send the key that checks its skins"));
        return;
    }
    finish(Result::Found);
}

void ElySkinLookup::refreshOfflineFace(const MinecraftAccountPtr& account)
{
    if (!account || account->accountType() != AccountType::Offline)
        return;
    auto* data = account->accountData();
    if (!data->offlineSkin) {
        if (!data->minecraftProfile.skin.data.isEmpty()) {
            data->minecraftProfile.skin = Skin();
            emit account->changed();
        }
        return;
    }
    const QString name = data->offlineSkinLookupName();
    auto* lookup = new ElySkinLookup(account.get());
    connect(lookup, &ElySkinLookup::finished, account.get(), [lookup, account, name](const Result& result) {
        lookup->deleteLater();
        auto* data = account->accountData();
        if (result.kind == Result::Failed || data->offlineSkinLookupName() != name)
            return;  // offline, or the name changed meanwhile: keep what is there
        if (result.kind == Result::NotFound) {
            if (!data->minecraftProfile.skin.data.isEmpty()) {
                data->minecraftProfile.skin = Skin();
                emit account->changed();
            }
            return;
        }
        QNetworkRequest request(result.skinUrl);
        request.setTransferTimeout(10000);
        auto* reply = APPLICATION->network()->get(request);
        connect(reply, &QNetworkReply::finished, account.get(), [reply, account, name, result] {
            reply->deleteLater();
            auto* data = account->accountData();
            const QByteArray png = reply->readAll();
            if (reply->error() != QNetworkReply::NoError || data->offlineSkinLookupName() != name || png == data->minecraftProfile.skin.data)
                return;
            data->minecraftProfile.skin.data = png;
            data->minecraftProfile.skin.id = result.elyName;
            data->minecraftProfile.skin.variant = result.slim ? "SLIM" : "CLASSIC";
            emit account->changed();
        });
    });
    lookup->start(name);
}

void ElySkinLookup::finish(Result::Kind kind, const QString& error)
{
    m_result.kind = kind;
    m_result.error = error;
    emit finished(m_result);
}
