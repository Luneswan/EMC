// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include "ServerStepTask.h"

class ServerInstance;

// Plugins and mods for servers, from Modrinth.
namespace ServerContent {

// The folder content goes into for a server: plugins/ or mods/ (hybrid servers have both; `mod`
// picks which one a Modrinth project belongs in).
QString contentDir(const ServerInstance* server, bool mod);
// True when a Modrinth project with these loaders is a mod rather than a plugin.
bool isModLoaderSet(const QStringList& loaders);

struct Starter {
    QString project;  // Modrinth id or slug
    QString name;
    QString description;
    bool recommended = false;
};
// Curated, well-maintained content suggested when creating a server of this type.
QList<Starter> starters(const QString& softwareId);

}  // namespace ServerContent

// Installs the newest compatible file of Modrinth projects into a folder, with their required
// dependencies. Projects already present (by file name) are skipped. A requested project may be
// pinned to one version id; one with no compatible version is reported, the rest still install.
class ModrinthServerInstallTask : public ServerStepTask {
    Q_OBJECT
   public:
    ModrinthServerInstallTask(QString project, QStringList loaders, QString mcVersion, QString dir, QObject* parent = nullptr)
        : ServerStepTask(parent), m_loaders(std::move(loaders)), m_mcVersion(std::move(mcVersion)), m_dir(std::move(dir))
    {
        m_queue.append(std::move(project));
        m_requested.insert(m_queue.first());
    }
    // project id -> version id ("" means newest compatible)
    ModrinthServerInstallTask(const QList<QPair<QString, QString>>& projects, QStringList loaders, QString mcVersion, QString dir,
                              QObject* parent = nullptr)
        : ServerStepTask(parent), m_loaders(std::move(loaders)), m_mcVersion(std::move(mcVersion)), m_dir(std::move(dir))
    {
        for (const auto& [project, version] : projects) {
            m_queue.append(project);
            m_requested.insert(project);
            if (!version.isEmpty()) {
                m_pinned.insert(project, version);
            }
        }
    }

    QStringList installedFiles() const { return m_installed; }
    QString projectTitle() const { return m_titles.value(0); }
    QStringList titles() const { return m_titles; }
    QStringList missing() const { return m_missing; }

   protected:
    void executeTask() override;

   private:
    void next();

    QStringList m_loaders;
    QString m_mcVersion;
    QString m_dir;
    QStringList m_queue;
    QSet<QString> m_seen;
    QSet<QString> m_requested;
    QHash<QString, QString> m_pinned;
    QStringList m_installed;
    QStringList m_titles;
    QStringList m_missing;
};

// Brings the plugins and mods in some folders up to date from Modrinth: each jar is looked up by its
// hash, and replaced by the newest release for this Minecraft version and the folder's loaders when
// that release is newer and made for a loader the installed file supports. Jars Modrinth does not
// know are left alone, and nothing is downgraded.
class ModrinthServerUpdateTask : public ServerStepTask {
    Q_OBJECT
   public:
    // Each folder with the loaders its content is for (plugins/ and mods/ differ on hybrid servers).
    using Folder = QPair<QString, QStringList>;
    ModrinthServerUpdateTask(QList<Folder> folders, QString mcVersion, QObject* parent = nullptr)
        : ServerStepTask(parent), m_folders(std::move(folders)), m_mcVersion(std::move(mcVersion))
    {}

    QStringList updated() const { return m_updated; }

   protected:
    void executeTask() override;

   private:
    void startFolder();
    void queryFolder();
    void post(const QString& path, const QJsonObject& body, std::function<void(const QJsonObject&)> next);
    void next();

    QList<Folder> m_folders;
    int m_folder = -1;
    QString m_mcVersion;
    QHash<QString, QString> m_files;  // sha1 -> jar path
    QJsonObject m_current;            // sha1 -> installed version
    QJsonObject m_latest;             // sha1 -> newest release
    QStringList m_queue;
    QStringList m_updated;
};
