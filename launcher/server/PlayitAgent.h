// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include <functional>

#include "tasks/Task.h"

class ServerInstance;

// playit.gg: a free tunnel that gives a server a public address when the router cannot forward
// ports (shared internet addresses, mobile hotspots, dorms). EMC links this computer to a playit
// account once (the user approves it on playit.gg), creates the tunnels each server needs, and
// runs the playit agent in the background while those servers run.
class PlayitAgent : public QObject {
    Q_OBJECT
   public:
    static constexpr const char* kVersion = "0.17.1";
    static PlayitAgent* instance();

    bool isLinked() const { return !m_secret.isEmpty(); }
    bool isLinking() const { return !m_code.isEmpty(); }
    QString claimUrl() const;
    // Starts linking: open claimUrl() in the browser, then EMC waits for the user to accept.
    void startLink();
    void cancelLink();
    void unlink();

    // Creates the server's tunnels if needed and stores its public addresses in its settings
    // (PlayitJava, PlayitBedrock). `done(error)`.
    void prepare(ServerInstance* server, std::function<void(QString)> done);
    // The agent runs while at least one server set to use playit runs.
    void setServerActive(ServerInstance* server, bool active);
    bool agentRunning() const { return m_process && m_process->state() != QProcess::NotRunning; }
    QString agentStatus() const { return m_agentStatus; }

   signals:
    void linkProgress(const QString& text);
    void linkedChanged();
    void agentStateChanged();

   private:
    explicit PlayitAgent(QObject* parent = nullptr);
    QString folder() const;
    QString agentPath() const;
    void api(const QString& path, const QJsonObject& body, std::function<void(QJsonValue, QString)> done);
    void pollClaim();
    void ensureAgent(std::function<void(QString)> done);
    void updateProcess();
    void waitForTunnels(ServerInstance* server, int attempts, std::function<void(QString)> done);
    void setAgentStatus(const QString& status);

    QString m_secret;
    QString m_code;
    QTimer m_claimPoll;
    int m_claimAttempts = 0;
    QPointer<QProcess> m_process;
    QList<QPointer<ServerInstance>> m_active;
    Task::Ptr m_download;
    QString m_agentStatus;
};
