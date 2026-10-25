// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "launch/LaunchStep.h"
#include "minecraft/auth/AuthSession.h"
#include "net/NetJob.h"

// Skin server accounts (Ely.by and friends) play through authlib-injector, a small Java agent that points the game's
// sign-in, skins and capes at that server. This step keeps the newest release in <data>/authlib-injector, checks its
// SHA-256, and hands its path to the launch. It does nothing for Microsoft and offline accounts.
class EnsureAuthlibInjector : public LaunchStep {
    Q_OBJECT
   public:
    EnsureAuthlibInjector(LaunchTask* parent, AuthSessionPtr session);
    ~EnsureAuthlibInjector() override = default;

    void executeTask() override;
    bool canAbort() const override { return true; }
    bool abort() override;

   private:
    void onLatestDone(QByteArray* response);
    void useCachedOrFail(const QString& reason);
    void succeedWith(const QString& jar);

    AuthSessionPtr m_session;
    NetJob::Ptr m_job;
    QString m_dir;
};
