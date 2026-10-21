// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QPointer>
#include <QStringList>

#include "ServerStepTask.h"

class ServerInstance;
class ServerList;

struct ServerCreateOptions {
    QString name;
    QString software = "paper";
    QString mcVersion;
    bool crossplay = false;
    bool acceptEula = false;

    // server.properties basics
    QString motd;
    QString gamemode = "survival";
    QString difficulty = "normal";
    QString seed;
    bool hardcore = false;
    bool pvp = true;
    bool onlineMode = true;  // off lets players with offline accounts join
    bool whitelist = false;
    int maxPlayers = 0;      // 0: from the optimizer

    bool autoOptimize = true;
    int maxMemMiB = 0;  // manual memory, when autoOptimize is off

    // Modrinth project ids or slugs installed after the server (plugins or mods, by server type).
    QStringList content;
};

// Creates a server from scratch: downloads the server software (running a loader installer where
// one is needed), the Java it needs, crossplay plugins, starter content, and writes its settings.
class ServerCreationTask : public ServerStepTask {
    Q_OBJECT
   public:
    ServerCreationTask(ServerList* list, ServerCreateOptions options, QObject* parent = nullptr);

    ServerInstance* server() const { return m_server; }

   protected:
    void executeTask() override;
    void emitFailed(QString reason = "") override;
    void emitAborted() override;

   private:
    void discard();
    void resolveJava();
    void installSoftware();
    void installVanilla();
    void installPaper();
    void installPurpur();
    void installFabric();
    void installForge();
    void installNeoForge();
    void runLoaderInstaller(const QString& url, const QString& fileName);
    void installArclight();
    void installBedrock();
    void installCrossplay();
    void installContent(int index);
    void finish();

    QString gameDir() const;
    void setLaunch(const QString& type, const QString& target);

    QPointer<ServerList> m_list;
    ServerCreateOptions m_options;
    QPointer<ServerInstance> m_server;
    QString m_javaPath;
    QString m_versionJsonUrl;
};
