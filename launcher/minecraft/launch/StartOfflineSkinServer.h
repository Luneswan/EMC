// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QPointer>

#include "launch/LaunchStep.h"
#include "minecraft/auth/AuthSession.h"
#include "minecraft/auth/ElySkinLookup.h"

class OfflineSkinServer;

// Offline accounts: looks the chosen name up on Ely.by and, when it has a skin, serves that skin to the game from
// 127.0.0.1 for as long as the game runs. Nothing here can stop the game from starting: without a skin (or without
// a connection) the player simply gets the default one.
class StartOfflineSkinServer : public LaunchStep {
    Q_OBJECT
   public:
    StartOfflineSkinServer(LaunchTask* parent, AuthSessionPtr session);
    ~StartOfflineSkinServer() override = default;

    void executeTask() override;
    void finalize() override;
    bool canAbort() const override { return true; }
    bool abort() override;

   private:
    void onLookup(const ElySkinLookup::Result& result);

    AuthSessionPtr m_session;
    ElySkinLookup* m_lookup = nullptr;
    QPointer<OfflineSkinServer> m_server;
};
