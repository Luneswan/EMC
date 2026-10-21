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
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "tasks/Task.h"

class QWidget;

// Finds Prism Launcher, PolyMC, MultiMC and UltimMC on this computer and brings their instances,
// worlds, accounts, icons, skins and Java into EMC. Everything is copied; the other launcher keeps its
// files. An instance is brought over once: a ledger in EMC's data folder remembers it, and one that
// is already in EMC (same folder, name and components) is recognised instead of copied again.
namespace LauncherImport {

struct Source {
    QString name;          // "Prism Launcher", "UltimMC", ...
    QString dir;           // the launcher's data folder
    QString instancesDir;  // where its instances live
};

// Looks in the usual data folders and, shallowly, where portable copies tend to be unpacked.
QList<Source> detect();

// Detects in the background at startup and imports anything new, showing progress over `parent`
// and a short summary afterwards. Does nothing when the "AutoImportLaunchers" setting is off.
void runAtStartup(QWidget* parent);

// Asks for a launcher folder (for a portable copy kept somewhere unusual) and imports it.
void importFromFolder(QWidget* parent);

}  // namespace LauncherImport

class LauncherImportTask : public Task {
    Q_OBJECT
   public:
    struct Result {
        QStringList instances;                // EMC instance ids that were added
        QHash<QString, QString> groups;       // EMC instance id -> group from the other launcher
        QHash<QString, QStringList> perSource;  // launcher name -> instance names brought over
        QStringList accountFiles;             // the other launchers' accounts.json, merged on the main thread
    };

    LauncherImportTask(QList<LauncherImport::Source> sources, QString dataDir);
    Result result() const { return m_result; }

    // True when a source has an instance EMC has not imported yet.
    static bool hasNew(const LauncherImport::Source& source, const QString& dataDir);

   protected:
    void executeTask() override;

   private:
    Result work();

    QList<LauncherImport::Source> m_sources;
    QString m_dataDir;
    Result m_result;
    QFutureWatcher<Result> m_watcher;
};
