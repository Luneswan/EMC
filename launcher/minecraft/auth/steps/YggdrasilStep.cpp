// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - Minecraft Launcher
 *  Sign-in against an authlib-injector skin server (Ely.by and friends).
 *  Based on FjordLauncher's YggdrasilStep, Copyright (C) 2023 Evan Goode <mail@evangoo.de>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "YggdrasilStep.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

#include "Application.h"
#include "minecraft/auth/Parsers.h"
#include "net/NetUtils.h"
#include "net/RawHeaderProxy.h"

namespace {
const QList<Net::HeaderPair> kJsonHeaders = { { "Content-Type", "application/json; charset=utf-8" }, { "Accept", "application/json" } };
}

YggdrasilStep::YggdrasilStep(AccountData* data, std::optional<QString> password) : AuthStep(data), m_password(std::move(password)) {}

QString YggdrasilStep::describe()
{
    return m_password ? tr("Signing in to %1.").arg(m_data->serverName()) : tr("Renewing the %1 session.").arg(m_data->serverName());
}

void YggdrasilStep::perform()
{
    if (m_data->clientToken().isEmpty())
        m_data->yggdrasilToken.extra["clientToken"] = QUuid::createUuid().toString(QUuid::Id128);

    if (!m_password) {
        refresh();
        return;
    }

    QJsonObject agent{ { "name", "Minecraft" }, { "version", 1 } };
    QJsonObject body{ { "agent", agent },
                      { "username", m_data->loginName() },
                      { "password", *m_password },
                      { "clientToken", m_data->clientToken() },
                      { "requestUser", true } };
    m_isRefresh = false;
    post("/authserver/authenticate", body);
}

void YggdrasilStep::refresh()
{
    QJsonObject body{ { "accessToken", m_data->yggdrasilToken.token }, { "clientToken", m_data->clientToken() }, { "requestUser", true } };
    m_isRefresh = true;
    post("/authserver/refresh", body);
}

void YggdrasilStep::post(const QString& endpoint, const QJsonObject& body)
{
    QUrl url(m_data->yggdrasilServer + endpoint);
    auto [request, response] = Net::Request::makeByteArray(url, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_request = request;
    m_request->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(kJsonHeaders));

    m_task.reset(new NetJob("YggdrasilStep", APPLICATION->network()));
    m_task->setAskRetry(false);
    m_task->addNetAction(m_request);

    connect(m_task.get(), &Task::finished, this, [this, response] { onRequestDone(response); });
    m_task->start();
}

void YggdrasilStep::onRequestDone(QByteArray* response)
{
    m_data->needsOneTimeCode = false;
    const auto error = m_request->error();
    const int status = m_request->replyStatusCode();

    QJsonParseError jsonError;
    const auto doc = QJsonDocument::fromJson(*response, &jsonError);
    const bool hasJson = jsonError.error == QJsonParseError::NoError && doc.isObject();

    if (error == QNetworkReply::NoError && status == 200 && hasJson) {
        processResponse(doc.object());
        return;
    }
    // The server answered with a reason (wrong password, expired token, two-factor code needed...)
    if (hasJson && doc.object().contains("errorMessage")) {
        processError(doc.object());
        return;
    }

    qWarning() << "Skin server sign-in failed:" << status << error << m_request->errorString() << QString::fromUtf8(*response);
    if (Net::isApplicationError(error) && !Net::isServerError(error)) {
        emit finished(AccountTaskState::STATE_FAILED_SOFT, tr("%1 refused the sign-in: %2").arg(m_data->serverName(), m_request->errorString()));
    } else {
        m_data->networkError = error;
        emit finished(AccountTaskState::STATE_OFFLINE,
                      tr("Couldn't reach %1. Check your internet connection and try again.").arg(m_data->serverName()));
    }
}

void YggdrasilStep::processResponse(const QJsonObject& response)
{
    const QString accessToken = response.value("accessToken").toString();
    if (accessToken.isEmpty()) {
        emit finished(AccountTaskState::STATE_FAILED_HARD, tr("%1 didn't send a session token.").arg(m_data->serverName()));
        return;
    }
    const QString clientToken = response.value("clientToken").toString();
    if (!clientToken.isEmpty())
        m_data->yggdrasilToken.extra["clientToken"] = clientToken;

    m_data->yggdrasilToken.token = accessToken;
    m_data->yggdrasilToken.validity = Validity::Certain;
    m_data->yggdrasilToken.issueInstant = QDateTime::currentDateTimeUtc();
    m_data->yggdrasilToken.notAfter = QDateTime();

    // Ely.by has one player per account. Servers with several pick the first; the player can sign in again to switch.
    QJsonObject profile = response.value("selectedProfile").toObject();
    if (profile.isEmpty()) {
        const auto available = response.value("availableProfiles").toArray();
        if (!available.isEmpty())
            profile = available.first().toObject();
    }
    const QString id = profile.value("id").toString();
    const QString name = profile.value("name").toString();
    if (id.isEmpty() || name.isEmpty()) {
        emit finished(AccountTaskState::STATE_FAILED_HARD,
                      tr("This %1 account has no Minecraft player yet. Create one on the %1 website, then sign in again.")
                          .arg(m_data->serverName()));
        return;
    }
    m_data->minecraftProfile.id = id;
    m_data->minecraftProfile.name = name;
    m_data->minecraftProfile.validity = Validity::Certain;
    // Skin server accounts can always play; there is nothing to buy.
    m_data->minecraftEntitlement.ownsMinecraft = true;
    m_data->minecraftEntitlement.canPlayMinecraft = true;
    m_data->minecraftEntitlement.validity = Validity::Certain;

    emit finished(AccountTaskState::STATE_WORKING, m_isRefresh ? tr("Session renewed") : tr("Signed in"));
}

void YggdrasilStep::processError(const QJsonObject& response)
{
    const QString error = response.value("error").toString();
    const QString message = response.value("errorMessage").toString();
    qWarning() << "Skin server error:" << error << message;

    // Ely.by asks for the authenticator code by refusing the password with this message; the code is then sent as
    // "password:code".
    if (!m_isRefresh && message.contains("two factor", Qt::CaseInsensitive)) {
        m_data->needsOneTimeCode = true;
        emit finished(AccountTaskState::STATE_FAILED_SOFT, tr("Enter the code from your authenticator app."));
        return;
    }
    if (m_isRefresh) {
        // The saved session is gone (signed out elsewhere, or too old). The player has to sign in again.
        emit finished(AccountTaskState::STATE_FAILED_HARD, tr("Your %1 session has expired. Sign in again.").arg(m_data->serverName()));
        return;
    }
    if (message.contains("Invalid credentials", Qt::CaseInsensitive)) {
        emit finished(AccountTaskState::STATE_FAILED_SOFT, tr("Wrong email, username or password."));
        return;
    }
    emit finished(AccountTaskState::STATE_FAILED_SOFT, message.isEmpty() ? error : message);
}

// --- profile ---------------------------------------------------------------------------------------------------

YggdrasilProfileStep::YggdrasilProfileStep(AccountData* data) : AuthStep(data) {}

QString YggdrasilProfileStep::describe()
{
    return tr("Getting your skin from %1.").arg(m_data->serverName());
}

void YggdrasilProfileStep::perform()
{
    QUrl url(m_data->yggdrasilServer + "/sessionserver/session/minecraft/profile/" + m_data->minecraftProfile.id + "?unsigned=true");
    auto [request, response] = Net::Request::makeByteArray(url);
    m_request = request;
    m_request->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(kJsonHeaders));

    m_task.reset(new NetJob("YggdrasilProfileStep", APPLICATION->network()));
    m_task->setAskRetry(false);
    m_task->addNetAction(m_request);
    connect(m_task.get(), &Task::finished, this, [this, response] { onRequestDone(response); });
    m_task->start();
}

void YggdrasilProfileStep::onRequestDone(QByteArray* response)
{
    // A missing or unreadable profile only costs the skin preview; the account still works.
    if (m_request->error() != QNetworkReply::NoError || response->isEmpty()) {
        qWarning() << "Skin server profile failed:" << m_request->replyStatusCode() << m_request->errorString();
        emit finished(AccountTaskState::STATE_WORKING, tr("Couldn't get the skin"));
        return;
    }
    MinecraftProfile profile;
    if (auto result = Parsers::parseMinecraftProfileMojang(*response, profile); !result) {
        qWarning() << "Skin server profile could not be parsed:" << result.error();
        emit finished(AccountTaskState::STATE_WORKING, tr("Couldn't read the skin"));
        return;
    }
    // Ely.by hands out plain http texture links; the same files are served over https.
    auto secure = [](QString& url) {
        QUrl u(url);
        if (u.scheme() == "http" && (u.host() == "ely.by" || u.host().endsWith(".ely.by"))) {
            u.setScheme("https");
            url = u.toString();
        }
    };
    secure(profile.skin.url);
    for (auto& cape : profile.capes)
        secure(cape.url);

    // Keep the name and id from the sign-in; take the textures from the profile.
    m_data->minecraftProfile.skin = profile.skin;
    m_data->minecraftProfile.capes = profile.capes;
    m_data->minecraftProfile.currentCape = profile.currentCape;
    emit finished(AccountTaskState::STATE_WORKING, tr("Got the profile"));
}

// --- metadata --------------------------------------------------------------------------------------------------

YggdrasilMetadataStep::YggdrasilMetadataStep(AccountData* data) : AuthStep(data) {}

QString YggdrasilMetadataStep::describe()
{
    return tr("Getting %1 server details.").arg(m_data->serverName());
}

void YggdrasilMetadataStep::perform()
{
    auto [request, response] = Net::Request::makeByteArray(QUrl(m_data->yggdrasilServer));
    m_request = request;
    m_task.reset(new NetJob("YggdrasilMetadataStep", APPLICATION->network()));
    m_task->setAskRetry(false);
    m_task->addNetAction(m_request);
    connect(m_task.get(), &Task::finished, this, [this, response] { onRequestDone(response); });
    m_task->start();
}

void YggdrasilMetadataStep::onRequestDone(QByteArray* response)
{
    QJsonParseError jsonError;
    QJsonDocument::fromJson(*response, &jsonError);
    if (m_request->error() == QNetworkReply::NoError && jsonError.error == QJsonParseError::NoError)
        m_data->yggdrasilMetadata = QString::fromLatin1(response->toBase64());
    emit finished(AccountTaskState::STATE_WORKING, tr("Got server details"));
}
