#pragma once

#include <QString>
#include <memory>

#include "LaunchMode.h"

class MinecraftAccount;

struct AuthSession {
    bool MakeOffline(QString offline_playername);
    void MakeDemo(QString name, QString uuid);

    QString serializeUserProperties();

    // combined session ID
    QString session;
    // volatile auth token
    QString access_token;
    // profile name
    QString player_name;
    // profile ID
    QString uuid;
    // 'msa' or 'offline', depending on account type
    QString user_type;
    // the actual launch mode for this session
    LaunchMode launchMode;
    // skin server accounts: the authlib-injector API root and its metadata (base64). Empty for everything else.
    QString yggdrasil_server;
    QString yggdrasil_metadata;
    // set at launch: the authlib-injector agent jar that points the game at that server
    QString authlib_injector_jar;
    // offline accounts: the Ely.by name whose skin to wear (empty: the default skin)
    QString offline_skin_name;
    // the skin server is EMC's own one for an offline skin: if it cannot be set up the game starts without it
    bool yggdrasil_optional = false;
};

using AuthSessionPtr = std::shared_ptr<AuthSession>;
