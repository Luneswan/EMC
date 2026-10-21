// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "ServerStepTask.h"

// The kinds of server EMC can create, and where each one's versions come from.
namespace ServerSoftware {

enum class Category { Vanilla, Plugins, Mods, Hybrid, Bedrock };

struct Info {
    QString id;
    QString name;
    QString description;
    Category category = Category::Vanilla;
    bool plugins = false;     // has a plugins/ folder (Bukkit API)
    bool mods = false;        // has a mods/ folder
    bool crossplay = false;   // Geyser + Floodgate can let Bedrock players in
    QStringList modrinthLoaders;  // loaders to search on Modrinth for this server's content
    bool advanced = false;
};

const QList<Info>& all();
const Info* find(const QString& id);
QString categoryName(Category category);
QString categoryDescription(Category category);

// Mojang makes the Bedrock Dedicated Server for Windows and Linux only.
bool bedrockSupported();
// The "downloadType" of this platform's server in Mojang's download list.
QString bedrockDownloadType();
// The server program inside the Bedrock folder.
QString bedrockExecutable();

}  // namespace ServerSoftware

struct ServerVersionEntry {
    QString mcVersion;  // the Minecraft version players need (Bedrock: the server version)
    QString label;      // shown in the version picker
    bool stable = true;
};

// Lists the Minecraft versions a server type supports, newest first.
class ServerVersionListTask : public ServerStepTask {
    Q_OBJECT
   public:
    explicit ServerVersionListTask(QString softwareId, QObject* parent = nullptr) : ServerStepTask(parent), m_software(std::move(softwareId))
    {}
    const QList<ServerVersionEntry>& versions() const { return m_versions; }

   protected:
    void executeTask() override;

   private:
    void finish();
    QString m_software;
    QList<ServerVersionEntry> m_versions;
};

// Shared knowledge about version naming.
namespace ServerVersions {
// "1.21.8" < "1.21.10" < "26.3"; non-numeric parts compare as text.
int compare(const QString& a, const QString& b);
bool isRelease(const QString& mcVersion);
// The NeoForge version prefix for a Minecraft version ("1.21.1" -> "21.1.", "26.3" -> "26.3.0.").
QString neoForgePrefix(const QString& mcVersion);
}  // namespace ServerVersions
