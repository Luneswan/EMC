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
#pragma once

#include <QJsonObject>
#include <QObject>
#include <optional>

#include "minecraft/auth/AuthStep.h"
#include "net/NetJob.h"
#include "net/Request.h"

// Signs in with a password (authenticate) or renews the saved token (refresh). The password is only
// held for this one request and never saved.
class YggdrasilStep : public AuthStep {
    Q_OBJECT
   public:
    explicit YggdrasilStep(AccountData* data, std::optional<QString> password);
    virtual ~YggdrasilStep() noexcept = default;

    void perform() override;
    QString describe() override;

   private slots:
    void onRequestDone(QByteArray* response);

   private:
    void post(const QString& endpoint, const QJsonObject& body);
    void refresh();
    void processResponse(const QJsonObject& response);
    void processError(const QJsonObject& response);

    std::optional<QString> m_password;
    bool m_isRefresh = false;
    Net::Request::Ptr m_request;
    NetJob::Ptr m_task;
};

// Reads the player's profile from the skin server's session server, with the skin and cape.
class YggdrasilProfileStep : public AuthStep {
    Q_OBJECT
   public:
    explicit YggdrasilProfileStep(AccountData* data);
    virtual ~YggdrasilProfileStep() noexcept = default;

    void perform() override;
    QString describe() override;

   private slots:
    void onRequestDone(QByteArray* response);

   private:
    Net::Request::Ptr m_request;
    NetJob::Ptr m_task;
};

// Fetches the server's API metadata once, so the game starts without another lookup. Never fails the sign-in.
class YggdrasilMetadataStep : public AuthStep {
    Q_OBJECT
   public:
    explicit YggdrasilMetadataStep(AccountData* data);
    virtual ~YggdrasilMetadataStep() noexcept = default;

    void perform() override;
    QString describe() override;

   private slots:
    void onRequestDone(QByteArray* response);

   private:
    Net::Request::Ptr m_request;
    NetJob::Ptr m_task;
};
