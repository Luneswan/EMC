// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerCreationTask.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include "FileSystem.h"
#include "ServerContent.h"
#include "ServerImporter.h"
#include "ServerNetwork.h"
#include "ServerInstance.h"
#include "ServerJavaTask.h"
#include "ServerList.h"
#include "ServerOptimizer.h"
#include "ServerProperties.h"
#include "ServerSoftware.h"
#include "archive/ExtractZipTask.h"
#include "settings/SettingsObject.h"

namespace {
// The lowest port at or above `start` that no other EMC server is set to use.
int freePort(ServerList* list, ServerInstance* self, int start, bool udp)
{
    QSet<int> used;
    if (list) {
        for (auto* s : list->servers()) {
            if (s != self) {
                used.insert(s->port());
                if (s->bedrockPort() > 0) {
                    used.insert(s->bedrockPort());
                }
            }
        }
    }
    return ServerNetwork::nextAvailablePort(start, udp, used);
}
}  // namespace

ServerCreationTask::ServerCreationTask(ServerList* list, ServerCreateOptions options, QObject* parent)
    : ServerStepTask(parent), m_list(list), m_options(std::move(options))
{}

QString ServerCreationTask::gameDir() const
{
    return m_server->gameDir();
}

void ServerCreationTask::setLaunch(const QString& type, const QString& target)
{
    m_server->settings()->set("LaunchType", type);
    m_server->settings()->set("LaunchTarget", target);
}

void ServerCreationTask::discard()
{
    // Leave nothing half-made behind.
    if (m_server && m_list) {
        auto* server = m_server.data();
        m_server = nullptr;
        m_list->remove(server, true);
    }
}

void ServerCreationTask::emitFailed(QString reason)
{
    discard();
    ServerStepTask::emitFailed(reason);
}

void ServerCreationTask::emitAborted()
{
    discard();
    ServerStepTask::emitAborted();
}

void ServerCreationTask::executeTask()
{
    if (!m_list) {
        emitFailed(tr("No server list."));
        return;
    }
    if (!ServerSoftware::find(m_options.software)) {
        emitFailed(tr("Unknown server type: %1").arg(m_options.software));
        return;
    }
    if (m_options.software != "bedrock" && !m_options.acceptEula) {
        emitFailed(tr("The Minecraft EULA must be accepted to run a server."));
        return;
    }
    m_server = m_list->add(m_list->newId(m_options.name));
    auto* s = m_server->settings();
    s->set("Name", m_options.name);
    s->set("Software", m_options.software);
    s->set("McVersion", m_options.mcVersion);
    s->set("Crossplay", m_options.crossplay && ServerSoftware::find(m_options.software)->crossplay);
    s->set("MemoryAuto", m_options.autoOptimize);
    if (!m_options.autoOptimize && m_options.maxMemMiB > 0) {
        s->set("MaxMemMiB", m_options.maxMemMiB);
        s->set("MinMemMiB", std::max(512, m_options.maxMemMiB / 2));
    }
    QDir().mkpath(gameDir());

    if (m_options.software == "bedrock") {
        installBedrock();
    } else {
        resolveJava();
    }
}

void ServerCreationTask::resolveJava()
{
    // Mojang's version file says which Java each Minecraft version needs; every Java server type uses it.
    setStatus(tr("Checking Minecraft %1").arg(m_options.mcVersion));
    fetchJson(QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"), [this](const QJsonDocument& manifest) {
        for (const auto v : manifest.object().value("versions").toArray()) {
            if (v.toObject().value("id").toString() == m_options.mcVersion) {
                m_versionJsonUrl = v.toObject().value("url").toString();
            }
        }
        if (m_versionJsonUrl.isEmpty()) {
            emitFailed(tr("Minecraft %1 was not found.").arg(m_options.mcVersion));
            return;
        }
        fetchJson(QUrl(m_versionJsonUrl), [this](const QJsonDocument& version) {
            const auto java = version.object().value("javaVersion").toObject();
            const QString javaName = java.value("component").toString("jre-legacy");
            const int major = java.value("majorVersion").toInt(8);
            m_server->settings()->set("JavaName", javaName);
            m_server->settings()->set("JavaMajor", major);
            auto task = makeShared<ServerJavaTask>(javaName, major);
            runSubTask(task, [this, task] {
                m_javaPath = task->javaPath();
                installSoftware();
            });
        });
    });
}

void ServerCreationTask::installSoftware()
{
    const QString sw = m_options.software;
    if (sw == "vanilla") {
        installVanilla();
    } else if (sw == "paper" || sw == "folia") {
        installPaper();
    } else if (sw == "purpur") {
        installPurpur();
    } else if (sw == "fabric") {
        installFabric();
    } else if (sw == "forge") {
        installForge();
    } else if (sw == "neoforge") {
        installNeoForge();
    } else if (sw.startsWith("arclight-")) {
        installArclight();
    } else {
        emitFailed(tr("Unknown server type: %1").arg(sw));
    }
}

void ServerCreationTask::installVanilla()
{
    fetchJson(QUrl(m_versionJsonUrl), [this](const QJsonDocument& version) {
        const auto server = version.object().value("downloads").toObject().value("server").toObject();
        if (server.isEmpty()) {
            emitFailed(tr("Mojang offers no server for Minecraft %1.").arg(m_options.mcVersion));
            return;
        }
        download(QUrl(server.value("url").toString()), FS::PathCombine(gameDir(), "server.jar"), server.value("sha1").toString(),
                 QCryptographicHash::Sha1, [this] {
                     setLaunch("jar", "server.jar");
                     installCrossplay();
                 });
    });
}

void ServerCreationTask::installPaper()
{
    const QString project = m_options.software;
    fetchJson(QUrl(QString("https://fill.papermc.io/v3/projects/%1/versions/%2/builds/latest").arg(project, m_options.mcVersion)),
              [this, project](const QJsonDocument& doc) {
                  const auto build = doc.object();
                  const auto file = build.value("downloads").toObject().value("server:default").toObject();
                  if (file.isEmpty()) {
                      emitFailed(tr("No %1 build exists for Minecraft %2 yet.").arg(project, m_options.mcVersion));
                      return;
                  }
                  m_server->settings()->set("Build", QString::number(build.value("id").toInt()));
                  const QString jar = project + ".jar";
                  download(QUrl(file.value("url").toString()), FS::PathCombine(gameDir(), jar),
                           file.value("checksums").toObject().value("sha256").toString(), QCryptographicHash::Sha256, [this, jar] {
                               setLaunch("jar", jar);
                               installCrossplay();
                           });
              });
}

void ServerCreationTask::installPurpur()
{
    const QString base = QString("https://api.purpurmc.org/v2/purpur/%1").arg(m_options.mcVersion);
    fetchJson(QUrl(base + "/latest"), [this, base](const QJsonDocument& doc) {
        const QString build = doc.object().value("build").toString();
        if (build.isEmpty()) {
            emitFailed(tr("No Purpur build exists for Minecraft %1 yet.").arg(m_options.mcVersion));
            return;
        }
        m_server->settings()->set("Build", build);
        download(QUrl(QString("%1/%2/download").arg(base, build)), FS::PathCombine(gameDir(), "purpur.jar"),
                 doc.object().value("md5").toString(), QCryptographicHash::Md5, [this] {
                     setLaunch("jar", "purpur.jar");
                     installCrossplay();
                 });
    });
}

void ServerCreationTask::installFabric()
{
    fetchJson(QUrl("https://meta.fabricmc.net/v2/versions/loader/" + m_options.mcVersion), [this](const QJsonDocument& loaders) {
        QString loader;
        for (const auto l : loaders.array()) {
            const auto o = l.toObject().value("loader").toObject();
            if (loader.isEmpty() || o.value("stable").toBool()) {
                loader = o.value("version").toString();
                if (o.value("stable").toBool()) {
                    break;
                }
            }
        }
        if (loader.isEmpty()) {
            emitFailed(tr("Fabric does not support Minecraft %1 yet.").arg(m_options.mcVersion));
            return;
        }
        fetchJson(QUrl("https://meta.fabricmc.net/v2/versions/installer"), [this, loader](const QJsonDocument& installers) {
            QString installer;
            for (const auto i : installers.array()) {
                if (i.toObject().value("stable").toBool()) {
                    installer = i.toObject().value("version").toString();
                    break;
                }
            }
            m_server->settings()->set("Build", loader);
            // Fabric's server launcher downloads the vanilla server and libraries on its first start.
            const QUrl url(QString("https://meta.fabricmc.net/v2/versions/loader/%1/%2/%3/server/jar").arg(m_options.mcVersion, loader, installer));
            download(url, FS::PathCombine(gameDir(), "fabric-server.jar"), {}, QCryptographicHash::Sha1, [this] {
                setLaunch("jar", "fabric-server.jar");
                QDir().mkpath(FS::PathCombine(gameDir(), "mods"));
                installCrossplay();
            });
        });
    });
}

void ServerCreationTask::installForge()
{
    fetchJson(QUrl("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json"), [this](const QJsonDocument& doc) {
        const auto promos = doc.object().value("promos").toObject();
        QString forge = promos.value(m_options.mcVersion + "-recommended").toString();
        if (forge.isEmpty()) {
            forge = promos.value(m_options.mcVersion + "-latest").toString();
        }
        if (forge.isEmpty()) {
            emitFailed(tr("Forge does not support Minecraft %1.").arg(m_options.mcVersion));
            return;
        }
        m_server->settings()->set("Build", forge);
        const QString full = m_options.mcVersion + "-" + forge;
        runLoaderInstaller(QString("https://maven.minecraftforge.net/net/minecraftforge/forge/%1/forge-%1-installer.jar").arg(full),
                           "forge-installer.jar");
    });
}

void ServerCreationTask::installNeoForge()
{
    fetchJson(QUrl("https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge"), [this](const QJsonDocument& doc) {
        const QString prefix = ServerVersions::neoForgePrefix(m_options.mcVersion);
        QString best;
        QString bestBeta;
        for (const auto v : doc.object().value("versions").toArray()) {
            const QString version = v.toString();
            if (!version.startsWith(prefix)) {
                continue;
            }
            QString& slot = version.contains("beta") ? bestBeta : best;
            if (slot.isEmpty() || ServerVersions::compare(version, slot) > 0) {
                slot = version;
            }
        }
        const QString neo = best.isEmpty() ? bestBeta : best;
        if (neo.isEmpty()) {
            emitFailed(tr("NeoForge does not support Minecraft %1.").arg(m_options.mcVersion));
            return;
        }
        m_server->settings()->set("Build", neo);
        runLoaderInstaller(QString("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/neoforge-%1-installer.jar").arg(neo),
                           "neoforge-installer.jar");
    });
}

void ServerCreationTask::runLoaderInstaller(const QString& url, const QString& fileName)
{
    const QString installer = FS::PathCombine(gameDir(), fileName);
    download(QUrl(url), installer, {}, QCryptographicHash::Sha1, [this, installer] {
        setStatus(tr("Installing the mod loader (this takes a minute)"));
        runProgram(m_javaPath, { "-jar", installer, "--installServer" }, gameDir(), [this, installer] {
            QFile::remove(installer);
            QFile::remove(installer + ".log");
            QDir().mkpath(FS::PathCombine(gameDir(), "mods"));

            // Modern Forge/NeoForge write a run script; reuse its arguments (minus the user args
            // file and %*, since EMC passes memory and flags itself).
            if (QStringList args; ServerImporter::readRunScript(gameDir(), &args)) {
                setLaunch("args", args.join(QChar('\n')));
                installCrossplay();
                return;
            }
            // Older Forge: a runnable server jar next to the libraries.
            static const QRegularExpression forgeJar("^(forge|neoforge)-.*\\.jar$");
            for (const auto& entry : QDir(gameDir()).entryList({ "*.jar" }, QDir::Files)) {
                if (forgeJar.match(entry).hasMatch() && !entry.contains("installer")) {
                    setLaunch("jar", entry);
                    installCrossplay();
                    return;
                }
            }
            emitFailed(tr("The mod loader installed, but EMC could not find how to start it."));
        });
    });
}

void ServerCreationTask::installArclight()
{
    const QString loader = m_options.software.mid(9);
    fetchJson(QUrl("https://api.github.com/repos/IzzelAliz/Arclight/releases?per_page=30"), [this, loader](const QJsonDocument& doc) {
        const QRegularExpression asset(QString("^arclight-%1-%2-.*\\.jar$").arg(loader, QRegularExpression::escape(m_options.mcVersion)));
        for (const auto r : doc.array()) {
            for (const auto a : r.toObject().value("assets").toArray()) {
                const auto o = a.toObject();
                if (!asset.match(o.value("name").toString()).hasMatch()) {
                    continue;
                }
                m_server->settings()->set("Build", r.toObject().value("tag_name").toString());
                QString digest = o.value("digest").toString();  // "sha256:..." on newer GitHub releases
                digest = digest.startsWith("sha256:") ? digest.mid(7) : QString();
                download(QUrl(o.value("browser_download_url").toString()), FS::PathCombine(gameDir(), "arclight.jar"), digest,
                         QCryptographicHash::Sha256, [this] {
                             setLaunch("jar", "arclight.jar");
                             QDir().mkpath(FS::PathCombine(gameDir(), "mods"));
                             QDir().mkpath(FS::PathCombine(gameDir(), "plugins"));
                             installCrossplay();
                         });
                return;
            }
        }
        emitFailed(tr("Arclight has no %1 build for Minecraft %2.").arg(loader, m_options.mcVersion));
    });
}

void ServerCreationTask::installBedrock()
{
    setStatus(tr("Finding the Bedrock server"));
    fetchJson(QUrl("https://net-secondary.web.minecraft-services.net/api/v1.0/download/links"), [this](const QJsonDocument& doc) {
        QString url;
        for (const auto l : doc.object().value("result").toObject().value("links").toArray()) {
            if (l.toObject().value("downloadType").toString() == ServerSoftware::bedrockDownloadType()) {
                url = l.toObject().value("downloadUrl").toString();
            }
        }
        if (url.isEmpty()) {
            emitFailed(ServerSoftware::bedrockSupported() ? tr("Mojang's Bedrock server download could not be found.")
                                                          : tr("Mojang makes the Bedrock server only for Windows and for Linux on Intel/AMD computers."));
            return;
        }
        static const QRegularExpression version("bedrock-server-([0-9.]+)\\.zip");
        m_server->settings()->set("McVersion", version.match(url).captured(1));
        m_server->settings()->set("Build", version.match(url).captured(1));
        const QString zip = FS::PathCombine(m_server->metaDir(), "bedrock-server.zip");
        download(QUrl(url), zip, {}, QCryptographicHash::Sha1, [this, zip] {
            setStatus(tr("Unpacking the Bedrock server"));
            runSubTask(makeShared<MMCZip::ExtractZipTask>(zip, QDir(gameDir())), [this, zip] {
                QFile::remove(zip);
                const QString exe = ServerSoftware::bedrockExecutable();
                // Zip files do not carry the executable bit.
                QFile program(FS::PathCombine(gameDir(), exe));
                program.setPermissions(program.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                setLaunch("exe", exe);
                finish();
            });
        });
    });
}

void ServerCreationTask::installCrossplay()
{
    if (!m_server->crossplay()) {
        installContent(0);
        return;
    }
    // Geyser translates Bedrock's protocol; Floodgate lets Bedrock players in without a Java account.
    setStatus(tr("Adding Bedrock crossplay (Geyser)"));
    const auto* info = m_server->softwareInfo();
    const QString geyserBase = "https://download.geysermc.org/v2/projects/%1/versions/latest/builds/latest/downloads/%2";
    if (info->plugins) {
        const QString dir = FS::PathCombine(gameDir(), "plugins");
        download(QUrl(geyserBase.arg("geyser", "spigot")), FS::PathCombine(dir, "Geyser-Spigot.jar"), {}, QCryptographicHash::Sha1, [=, this] {
            download(QUrl(geyserBase.arg("floodgate", "spigot")), FS::PathCombine(dir, "floodgate-spigot.jar"), {}, QCryptographicHash::Sha1,
                     [this] { installContent(0); });
        });
        return;
    }
    const QString loader = m_options.software;  // fabric or neoforge
    const QString dir = FS::PathCombine(gameDir(), "mods");
    const QString jar = loader == "fabric" ? "Geyser-Fabric.jar" : "Geyser-NeoForge.jar";
    download(QUrl(geyserBase.arg("geyser", loader)), FS::PathCombine(dir, jar), {}, QCryptographicHash::Sha1, [=, this] {
        auto floodgate = makeShared<ModrinthServerInstallTask>("floodgate", QStringList{ loader }, m_options.mcVersion, dir);
        runSubTask(floodgate, [=, this] {
            if (loader != "fabric") {
                installContent(0);
                return;
            }
            runSubTask(makeShared<ModrinthServerInstallTask>("fabric-api", QStringList{ "fabric" }, m_options.mcVersion, dir),
                       [this] { installContent(0); });
        });
    });
}

void ServerCreationTask::installContent(int index)
{
    if (index >= m_options.content.size()) {
        finish();
        return;
    }
    const auto* info = m_server->softwareInfo();
    const QString project = m_options.content[index];
    // A hybrid server takes both; try the plugin loaders first, then the mod loaders.
    const QStringList loaders = info->modrinthLoaders;
    const QStringList pluginLoaders = { "paper", "purpur", "folia", "spigot", "bukkit" };
    QStringList plugins;
    QStringList mods;
    for (const auto& l : loaders) {
        (pluginLoaders.contains(l) ? plugins : mods) << l;
    }
    auto tryInstall = [this, index, project](const QStringList& set, bool mod, std::function<void()> otherwise) {
        auto task = makeShared<ModrinthServerInstallTask>(project, set, m_options.mcVersion, ServerContent::contentDir(m_server, mod));
        m_current = task;
        connect(task.get(), &Task::succeeded, this, [this, index] {
            QMetaObject::invokeMethod(this, [this, index] { installContent(index + 1); }, Qt::QueuedConnection);
        });
        connect(task.get(), &Task::failed, this, [this, otherwise, project, index](const QString& reason) {
            QMetaObject::invokeMethod(
                this,
                [this, otherwise, project, reason, index] {
                    if (otherwise) {
                        otherwise();
                    } else {
                        // Starter content is a nicety: the server is still fine without it.
                        logWarning(tr("Skipped %1: %2").arg(project, reason));
                        installContent(index + 1);
                    }
                },
                Qt::QueuedConnection);
        });
        connect(task.get(), &Task::status, this, &ServerCreationTask::setStatus);
        task->start();
    };
    if (!plugins.isEmpty()) {
        std::function<void()> fallback;
        if (!mods.isEmpty()) {
            fallback = [tryInstall, mods] { tryInstall(mods, true, {}); };
        }
        tryInstall(plugins, false, fallback);
    } else {
        tryInstall(mods, true, {});
    }
}

void ServerCreationTask::finish()
{
    setStatus(tr("Writing settings"));
    const bool bedrock = m_server->isBedrock();
    const auto* info = m_server->softwareInfo();
    auto plan = ServerOptimizer::plan(ServerOptimizer::Machine::current(), info && info->mods, bedrock);
    if (m_options.maxPlayers > 0) {
        plan.maxPlayers = m_options.maxPlayers;
    }

    ServerProperties props;
    props.setUnicodeEscapes(!bedrock);
    props.load(m_server->propertiesPath());  // BDS ships a commented one; keep its comments
    ServerOptimizer::apply(plan, props, bedrock);
    const QString seed = m_options.seed.trimmed();
    if (bedrock) {
        props.setValue("server-name", m_options.motd.isEmpty() ? m_options.name : m_options.motd);
        props.setValue("gamemode", m_options.gamemode);
        props.setValue("difficulty", m_options.difficulty);
        props.setValue("level-seed", seed);
        props.setValue("allow-list", m_options.whitelist);
        props.setValue("online-mode", m_options.onlineMode);
        const int port = freePort(m_list, m_server, 19132, true);
        props.setValue("server-port", port);
        props.setValue("server-portv6", freePort(m_list, m_server, port + 1, true));
    } else {
        props.setValue("motd", m_options.motd.isEmpty() ? m_options.name : m_options.motd);
        props.setValue("gamemode", m_options.gamemode);
        props.setValue("difficulty", m_options.difficulty);
        props.setValue("level-seed", seed);
        props.setValue("hardcore", m_options.hardcore);
        props.setValue("pvp", m_options.pvp);
        props.setValue("online-mode", m_options.onlineMode);
        props.setValue("white-list", m_options.whitelist);
        props.setValue("enforce-whitelist", m_options.whitelist);
        props.setValue("server-port", freePort(m_list, m_server, 25565, false));
        if (m_server->crossplay()) {
            m_server->settings()->set("BedrockPort", freePort(m_list, m_server, 19132, true));
        }

        QFile eula(FS::PathCombine(gameDir(), "eula.txt"));
        if (eula.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            eula.write("# Accepted in EMC when this server was created (https://aka.ms/MinecraftEULA)\neula=true\n");
        }
    }
    if (!props.save(m_server->propertiesPath())) {
        emitFailed(tr("Could not write server.properties."));
        return;
    }
    emit m_server->changed();
    emitSucceeded();
}
