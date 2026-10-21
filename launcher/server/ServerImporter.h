// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QFutureWatcher>
#include <QPointer>
#include <QStringList>

#include "ServerStepTask.h"

class ServerInstance;
class ServerList;

// What EMC could tell about a server folder someone already has.
struct ServerScan {
    QString dir;
    bool valid = false;
    QString problem;  // why the folder cannot be added, when !valid

    QString name;      // a suggestion, from the folder name
    QString software;  // a ServerSoftware id
    QString softwareNote;  // e.g. "Spigot (treated as Paper for plugins)"
    QString mcVersion;     // empty when it could not be found
    QString launchType;    // jar | args | exe
    QString launchTarget;
    int port = 0;
    int plugins = 0;
    int mods = 0;
    QStringList worlds;
    bool crossplay = false;
    bool eulaAccepted = false;
    bool running = false;  // another program has the world open
};

namespace ServerImporter {
// Looks at a folder and works out what kind of server it is and how to start it.
ServerScan scan(const QString& dir);
// The start arguments in a modern Forge/NeoForge run.bat, minus the user args file, %* and nogui.
bool readRunScript(const QString& dir, QStringList* args);
// "21.1.77" -> "1.21.1", "26.3.0.95" -> "26.3".
QString mcForNeoForge(const QString& neoForgeVersion);
}  // namespace ServerImporter

// Adds a scanned folder to EMC: in place (EMC runs it where it is) or as a copy, and finds the
// Java it needs.
class ServerImportTask : public ServerStepTask {
    Q_OBJECT
   public:
    ServerImportTask(ServerList* list, ServerScan scan, QString name, bool copy, bool acceptEula, QObject* parent = nullptr);
    ServerInstance* server() const { return m_server; }

    // The copy runs on another thread and cannot be interrupted, so it has to finish first.
    bool canAbort() const override { return !m_copyWatcher.isRunning(); }
    bool abort() override;

   protected:
    void executeTask() override;
    void emitFailed(QString reason = "") override;
    void emitAborted() override;

   private:
    void discard();
    void configure();
    void resolveJava();

    QPointer<ServerList> m_list;
    ServerScan m_scan;
    QString m_name;
    bool m_copy;
    bool m_acceptEula;
    QPointer<ServerInstance> m_server;
    QFutureWatcher<bool> m_copyWatcher;
};
