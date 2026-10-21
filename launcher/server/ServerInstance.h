// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <memory>

#include "LoggedProcess.h"
#include "ServerSoftware.h"
#include "settings/SettingsObject.h"
#include "tasks/Task.h"

class LogModel;
class SettingsObject;

// One server EMC manages: its folder, its settings (emc-server.cfg next to the server files' folder)
// and, while it runs, its process and console.
class ServerInstance : public QObject {
    Q_OBJECT
   public:
    enum class Status { Stopped, Preparing, Starting, Running, Stopping, Crashed };

    ServerInstance(QString id, QString metaDir, QObject* parent = nullptr);
    ~ServerInstance() override;

    QString id() const { return m_id; }
    QString metaDir() const { return m_metaDir; }
    QString gameDir() const;  // where the server's own files live
    SettingsObject* settings() const { return m_settings.get(); }

    QString name() const;
    void setName(const QString& name);
    QString software() const;
    const ServerSoftware::Info* softwareInfo() const;
    bool isBedrock() const { return software() == "bedrock"; }
    QString mcVersion() const;
    QString propertiesPath() const;
    int port() const;         // the port players connect to
    int bedrockPort() const;  // Geyser's UDP port when crossplay is on, else 0
    bool crossplay() const;
    QString typeLabel() const;  // "Paper 1.21.8", "Bedrock 1.26.52"

    Status status() const { return m_status; }
    QString statusText() const;
    bool isActive() const { return m_status != Status::Stopped && m_status != Status::Crashed; }
    QStringList players() const { return m_players; }
    QDateTime startedAt() const { return m_startedAt; }
    LogModel* log() const { return m_log; }
    QString lastError() const { return m_lastError; }

    // Memory the server will be given on its next start, after the optimizer when set to auto.
    int effectiveMaxMemMiB() const;

    // How friends on the internet reach this server: "off", "upnp" (the router forwards the port)
    // or "playit" (a playit.gg tunnel).
    QString publicMode() const;
    // The addresses friends type, for the current public mode (empty while unknown).
    QStringList publicAddresses() const;
    // The user's own domain once its DNS records were checked, else empty.
    QString customDomain() const;
    // name.duckdns.org when a free DuckDNS name is set up, else empty.
    QString duckDnsHost() const;
    // The program that listens for players: java.exe or bedrock_server.exe. Empty until Java is downloaded.
    QString serverProgram() const;
    // A line in the console from EMC itself.
    void logLine(const QString& line, MessageLevel level = MessageLevel::Launcher) { appendLauncherLine(line, level); }

   public slots:
    void start();
    void stop();
    void restart();
    void kill();
    void sendCommand(const QString& command);

   signals:
    void statusChanged(ServerInstance::Status status);
    void playersChanged();
    void changed();  // name, type or settings changed
    void ready();    // the server finished starting and accepts players

   private:
    void setStatus(Status status);
    void launchProcess(const QString& java);
    void syncGeyserPort();
    void ensurePortsFree();
    void onLog(const QStringList& lines, MessageLevel level);
    void onProcessState(LoggedProcess::State state);
    void appendLauncherLine(const QString& line, MessageLevel level = MessageLevel::Launcher);
    QStringList buildArguments(const QString& java, QString* program) const;

    QString m_id;
    QString m_metaDir;
    std::unique_ptr<SettingsObject> m_settings;
    LogModel* m_log;
    QPointer<LoggedProcess> m_process;
    Task::Ptr m_prepare;
    Status m_status = Status::Stopped;
    QStringList m_players;
    QDateTime m_startedAt;
    QString m_lastError;
    QTimer m_killTimer;
    bool m_restartAfterStop = false;
    bool m_firewallChecked = false;
    bool m_contentChecked = false;  // plugins and mods were checked for updates for this start
    QList<QDateTime> m_recentCrashes;
};
