// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerSoftware.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace ServerSoftware {

const QList<Info>& all()
{
    static const QList<Info> list = {
        { "vanilla", QObject::tr("Vanilla"), QObject::tr("The official Minecraft server from Mojang. No plugins or mods."), Category::Vanilla,
          false, false, false, {}, false },
        { "paper", QObject::tr("Paper"),
          QObject::tr("Fast, stable and the most popular choice. Runs Bukkit, Spigot and Paper plugins."), Category::Plugins, true,
          false, true, { "paper", "spigot", "bukkit" }, false },
        { "purpur", QObject::tr("Purpur"), QObject::tr("Paper with many extra gameplay options. Runs the same plugins."), Category::Plugins,
          true, false, true, { "purpur", "paper", "spigot", "bukkit" }, false },
        { "folia", QObject::tr("Folia"),
          QObject::tr("Paper split across CPU cores, for very large servers. Many plugins are not compatible."), Category::Plugins, true,
          false, true, { "folia" }, true },
        { "fabric", QObject::tr("Fabric"), QObject::tr("Lightweight mod loader with fast updates and great performance mods."),
          Category::Mods, false, true, true, { "fabric" }, false },
        { "neoforge", QObject::tr("NeoForge"), QObject::tr("The modern Forge successor, home of most big modpacks."), Category::Mods, false,
          true, true, { "neoforge" }, false },
        { "forge", QObject::tr("Forge"), QObject::tr("The classic mod loader, for older modpacks."), Category::Mods, false, true, false,
          { "forge" }, false },
        { "arclight-neoforge", QObject::tr("Arclight (NeoForge)"), QObject::tr("NeoForge mods and Bukkit plugins on one server."),
          Category::Hybrid, true, true, true, { "neoforge", "spigot", "bukkit" }, true },
        { "arclight-fabric", QObject::tr("Arclight (Fabric)"), QObject::tr("Fabric mods and Bukkit plugins on one server."), Category::Hybrid,
          true, true, true, { "fabric", "spigot", "bukkit" }, true },
        { "arclight-forge", QObject::tr("Arclight (Forge)"), QObject::tr("Forge mods and Bukkit plugins on one server."), Category::Hybrid,
          true, true, true, { "forge", "spigot", "bukkit" }, true },
        { "bedrock", QObject::tr("Bedrock Dedicated Server"),
          QObject::tr("Mojang's official Bedrock server. Bedrock players only (Windows 10/11, phones, consoles)."), Category::Bedrock, false,
          false, false, {}, false },
    };
    return list;
}

const Info* find(const QString& id)
{
    for (const auto& info : all()) {
        if (info.id == id) {
            return &info;
        }
    }
    return nullptr;
}

QString categoryName(Category category)
{
    switch (category) {
        case Category::Vanilla:
            return QObject::tr("Vanilla");
        case Category::Plugins:
            return QObject::tr("Plugins");
        case Category::Mods:
            return QObject::tr("Mods");
        case Category::Hybrid:
            return QObject::tr("Mods + Plugins");
        case Category::Bedrock:
            return QObject::tr("Bedrock");
    }
    return {};
}

QString categoryDescription(Category category)
{
    switch (category) {
        case Category::Vanilla:
            return QObject::tr("Pure Minecraft, exactly as Mojang ships it.");
        case Category::Plugins:
            return QObject::tr("Add plugins for ranks, economy, protection, minigames and more. Players join with an unmodified game.");
        case Category::Mods:
            return QObject::tr("Add mods that change the game itself. Players need the same mods installed.");
        case Category::Hybrid:
            return QObject::tr("Mods and plugins together. Powerful, but less stable than either alone.");
        case Category::Bedrock:
            return QObject::tr("For players on Bedrock Edition.");
    }
    return {};
}

}  // namespace ServerSoftware

namespace ServerVersions {

int compare(const QString& a, const QString& b)
{
    static const QRegularExpression split("[.\\-+_]");
    const auto pa = a.split(split);
    const auto pb = b.split(split);
    for (int i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        if (i >= pa.size()) {
            return -1;
        }
        if (i >= pb.size()) {
            return 1;
        }
        bool okA = false;
        bool okB = false;
        const int na = pa[i].toInt(&okA);
        const int nb = pb[i].toInt(&okB);
        if (okA && okB) {
            if (na != nb) {
                return na < nb ? -1 : 1;
            }
        } else if (okA != okB) {
            return okA ? 1 : -1;  // "1.21" > "1.21-rc1"
        } else if (const int c = pa[i].compare(pb[i]); c != 0) {
            return c < 0 ? -1 : 1;
        }
    }
    return 0;
}

bool isRelease(const QString& mcVersion)
{
    static const QRegularExpression release("^\\d+(\\.\\d+)+$");
    return release.match(mcVersion).hasMatch();
}

QString neoForgePrefix(const QString& mcVersion)
{
    auto parts = mcVersion.split('.');
    if (parts.value(0) == "1") {
        return QString("%1.%2.").arg(parts.value(1), parts.value(2, "0"));
    }
    while (parts.size() < 3) {
        parts.append("0");
    }
    return parts.join('.') + '.';
}

// The Minecraft version a NeoForge version is for ("21.1.77" -> "1.21.1", "26.3.0.95" -> "26.3").
static QString mcForNeoForge(const QString& neo)
{
    const auto parts = neo.split('-').first().split('.');
    if (parts.size() < 3) {
        return {};
    }
    bool ok = false;
    const int major = parts[0].toInt(&ok);
    if (!ok) {
        return {};
    }
    if (major < 25) {
        return parts[1] == "0" ? QString("1.%1").arg(parts[0]) : QString("1.%1.%2").arg(parts[0], parts[1]);
    }
    if (parts.size() < 4) {
        return {};
    }
    return parts[2] == "0" ? QString("%1.%2").arg(parts[0], parts[1]) : QString("%1.%2.%3").arg(parts[0], parts[1], parts[2]);
}

}  // namespace ServerVersions

void ServerVersionListTask::finish()
{
    // Newest first, without duplicates.
    QSet<QString> seen;
    QList<ServerVersionEntry> unique;
    for (const auto& v : m_versions) {
        if (!seen.contains(v.mcVersion)) {
            seen.insert(v.mcVersion);
            unique.append(v);
        }
    }
    std::stable_sort(unique.begin(), unique.end(),
                     [](const ServerVersionEntry& a, const ServerVersionEntry& b) { return ServerVersions::compare(a.mcVersion, b.mcVersion) > 0; });
    m_versions = unique;
    if (m_versions.isEmpty()) {
        emitFailed(tr("No versions were found."));
        return;
    }
    emitSucceeded();
}

void ServerVersionListTask::executeTask()
{
    setStatus(tr("Finding versions"));
    auto add = [this](const QString& mc, bool stable = true) { m_versions.append({ mc, mc, stable }); };

    if (m_software == "vanilla") {
        fetchJson(QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"), [this, add](const QJsonDocument& doc) {
            for (const auto v : doc.object().value("versions").toArray()) {
                const auto o = v.toObject();
                if (o.value("type").toString() == "release") {
                    add(o.value("id").toString());
                }
            }
            finish();
        });
    } else if (m_software == "paper" || m_software == "folia") {
        fetchJson(QUrl("https://fill.papermc.io/v3/projects/" + m_software), [this, add](const QJsonDocument& doc) {
            const auto groups = doc.object().value("versions").toObject();
            for (auto it = groups.begin(); it != groups.end(); ++it) {
                for (const auto v : it.value().toArray()) {
                    if (ServerVersions::isRelease(v.toString())) {
                        add(v.toString());
                    }
                }
            }
            finish();
        });
    } else if (m_software == "purpur") {
        fetchJson(QUrl("https://api.purpurmc.org/v2/purpur"), [this, add](const QJsonDocument& doc) {
            for (const auto v : doc.object().value("versions").toArray()) {
                add(v.toString());
            }
            finish();
        });
    } else if (m_software == "fabric") {
        fetchJson(QUrl("https://meta.fabricmc.net/v2/versions/game"), [this, add](const QJsonDocument& doc) {
            for (const auto v : doc.array()) {
                if (v.toObject().value("stable").toBool()) {
                    add(v.toObject().value("version").toString());
                }
            }
            finish();
        });
    } else if (m_software == "forge") {
        fetchJson(QUrl("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json"), [this, add](const QJsonDocument& doc) {
            const auto promos = doc.object().value("promos").toObject();
            for (auto it = promos.begin(); it != promos.end(); ++it) {
                if (it.key().endsWith("-latest")) {
                    const QString mc = it.key().chopped(7);
                    // Forge before 1.7 ran on a very different server setup; not offered.
                    if (ServerVersions::compare(mc, "1.7.10") >= 0) {
                        add(mc);
                    }
                }
            }
            finish();
        });
    } else if (m_software == "neoforge") {
        fetchJson(QUrl("https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge"), [this, add](const QJsonDocument& doc) {
            for (const auto v : doc.object().value("versions").toArray()) {
                const QString mc = ServerVersions::mcForNeoForge(v.toString());
                if (!mc.isEmpty() && ServerVersions::isRelease(mc)) {
                    add(mc, !v.toString().contains("beta"));
                }
            }
            finish();
        });
    } else if (m_software.startsWith("arclight-")) {
        const QString loader = m_software.mid(9);
        fetchJson(QUrl("https://api.github.com/repos/IzzelAliz/Arclight/releases?per_page=30"), [this, add, loader](const QJsonDocument& doc) {
            const QRegularExpression asset(QString("^arclight-%1-([0-9.]+)-.*\\.jar$").arg(loader));
            for (const auto r : doc.array()) {
                for (const auto a : r.toObject().value("assets").toArray()) {
                    const auto m = asset.match(a.toObject().value("name").toString());
                    if (m.hasMatch()) {
                        add(m.captured(1), !r.toObject().value("prerelease").toBool());
                    }
                }
            }
            finish();
        });
    } else if (m_software == "bedrock") {
        fetchJson(QUrl("https://net-secondary.web.minecraft-services.net/api/v1.0/download/links"), [this, add](const QJsonDocument& doc) {
            static const QRegularExpression version("bedrock-server-([0-9.]+)\\.zip");
            for (const auto l : doc.object().value("result").toObject().value("links").toArray()) {
                const auto o = l.toObject();
                if (o.value("downloadType").toString() == "serverBedrockWindows") {
                    const auto m = version.match(o.value("downloadUrl").toString());
                    if (m.hasMatch()) {
                        add(m.captured(1));
                    }
                }
            }
            finish();
        });
    } else {
        emitFailed(tr("Unknown server type: %1").arg(m_software));
    }
}
