// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <QDateTime>
#include <QMap>
#include <QNetworkReply>
#include <QVariantMap>

enum class Validity { None, Assumed, Certain };

struct Token {
    QDateTime issueInstant;
    QDateTime notAfter;
    QString token;
    QString refresh_token;
    QVariantMap extra;

    Validity validity = Validity::None;
    bool persistent = true;
};

struct Skin {
    QString id;
    QString url;
    QString variant;

    QByteArray data;
};

struct Cape {
    QString id;
    QString url;
    QString alias;

    QByteArray data;
};

struct MinecraftEntitlement {
    bool ownsMinecraft = false;
    bool canPlayMinecraft = false;
    Validity validity = Validity::None;
};

struct MinecraftProfile {
    QString id;
    QString name;
    Skin skin;
    QString currentCape;
    QMap<QString, Cape> capes;
    Validity validity = Validity::None;
};

// Yggdrasil: an account on a skin server that speaks the authlib-injector API (Ely.by by default).
// The game gets its skins, capes and multiplayer sign-in from that server instead of Mojang.
enum class AccountType { MSA, Offline, Yggdrasil };

enum class AccountState { Unchecked, Offline, Working, Online, Disabled, Errored, Expired, Gone };

struct AccountData {
    QJsonObject saveState() const;
    bool resumeStateFromV3(QJsonObject data);

    //! Yggdrasil access token, as passed to the game.
    QString accessToken() const;

    QString profileId() const;
    QString profileName() const;

    QString lastError() const;

    //! Yggdrasil accounts: the server's API root, e.g. https://authserver.ely.by/api/authlib-injector
    QString yggdrasilServer;
    //! Yggdrasil accounts: the server's API metadata, base64, handed to authlib-injector so it skips a lookup
    QString yggdrasilMetadata;
    //! Short display name of the skin server ("Ely.by", "LittleSkin" or its host name)
    QString serverName() const;
    //! The name the player signs in with (email or username), kept so a new sign-in can be prefilled
    QString loginName() const;
    QString clientToken() const;

    //! Offline accounts: wear the Ely.by skin of offlineSkinName (or of the player name when that is empty)
    bool offlineSkin = true;
    QString offlineSkinName;
    QString offlineSkinLookupName() const { return offlineSkinName.isEmpty() ? profileName() : offlineSkinName; }

    AccountType type = AccountType::MSA;

    QString msaClientID;
    Token msaToken;
    Token userToken;
    Token mojangservicesToken;

    Token yggdrasilToken;
    MinecraftProfile minecraftProfile;
    MinecraftEntitlement minecraftEntitlement;
    Validity validity_ = Validity::None;

    // runtime only information (not saved with the account)
    QString internalId;
    QString errorString;
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    AccountState accountState = AccountState::Unchecked;
    //! Yggdrasil sign-in was refused until the authenticator app's code is added
    bool needsOneTimeCode = false;
};
