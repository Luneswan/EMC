// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerImporter.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QtConcurrent>

#include "FileSystem.h"
#include "ServerInstance.h"
#include "ServerJavaTask.h"
#include "ServerList.h"
#include "ServerProperties.h"
#include "ServerSoftware.h"

namespace {
QString readText(const QString& path, qint64 limit = 4 * 1024 * 1024)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.read(limit));
}

int countJars(const QString& dir)
{
    return int(QDir(dir).entryList({ "*.jar" }, QDir::Files).size());
}

QString firstSubdir(const QString& dir)
{
    const auto entries = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    return entries.value(0);
}

// Picks the jar most likely to be the server itself.
QString serverJar(const QStringList& jars)
{
    static const QStringList preferred{ "paper", "purpur", "folia", "spigot", "craftbukkit", "fabric-server", "arclight",
                                        "forge", "neoforge", "minecraft_server", "server" };
    for (const auto& prefix : preferred) {
        for (const auto& jar : jars) {
            if (jar.toLower().startsWith(prefix) && !jar.contains("installer", Qt::CaseInsensitive)) {
                return jar;
            }
        }
    }
    return jars.size() == 1 ? jars.first() : QString();
}

bool worldInUse(const QString& worldDir)
{
    // A running Java server holds session.lock open; on Windows that makes it impossible to remove.
    const QString lock = FS::PathCombine(worldDir, "session.lock");
    if (!QFileInfo::exists(lock)) {
        return false;
    }
    QFile file(lock);
    if (!file.open(QIODevice::ReadWrite)) {
        return true;
    }
    return false;
}
}  // namespace

QString ServerImporter::mcForNeoForge(const QString& neoForgeVersion)
{
    const auto parts = neoForgeVersion.split('.');
    if (parts.size() < 2) {
        return {};
    }
    if (parts[0].toInt() >= 26) {
        // Year-based versions: 26.3.0.95 -> 26.3, 26.3.1.4 -> 26.3.1
        QString mc = parts[0] + '.' + parts[1];
        if (parts.size() > 3 && parts[2] != "0") {
            mc += '.' + parts[2];
        }
        return mc;
    }
    return parts[1] == "0" ? QString("1.%1").arg(parts[0]) : QString("1.%1.%2").arg(parts[0], parts[1]);
}

bool ServerImporter::readRunScript(const QString& dir, QStringList* args)
{
    // The installer writes both scripts; a folder from elsewhere may have only one.
    for (const auto* script : { "run.bat", "run.sh" }) {
        const QString text = readText(FS::PathCombine(dir, script));
        for (QString line : text.split('\n')) {
            line = line.trimmed();
            if (!line.startsWith("java ")) {
                continue;
            }
            QStringList parts = QProcess::splitCommand(line);
            parts.removeFirst();
            parts.removeAll("@user_jvm_args.txt");
            parts.removeAll("%*");
            parts.removeAll("$@");
            parts.removeAll("nogui");
            if (!parts.isEmpty()) {
                *args = parts;
                return true;
            }
        }
    }
    return false;
}

ServerScan ServerImporter::scan(const QString& dirPath)
{
    ServerScan s;
    const QDir dir(dirPath);
    s.dir = QDir::cleanPath(dir.absolutePath());
    s.name = dir.dirName();
    if (!dir.exists()) {
        s.problem = QObject::tr("The folder does not exist.");
        return s;
    }

    ServerProperties props;
    const bool hasProps = props.load(dir.filePath("server.properties"));
    s.eulaAccepted = readText(dir.filePath("eula.txt")).contains(QRegularExpression("^\\s*eula\\s*=\\s*true", QRegularExpression::MultilineOption |
                                                                                                                QRegularExpression::CaseInsensitiveOption));
    s.plugins = countJars(dir.filePath("plugins"));
    s.mods = countJars(dir.filePath("mods"));
    for (const QString& folder : { QString("plugins"), QString("mods") }) {
        for (const auto& jar : QDir(dir.filePath(folder)).entryList({ "*.jar" }, QDir::Files)) {
            if (jar.startsWith("geyser", Qt::CaseInsensitive)) {
                s.crossplay = true;
            }
        }
    }

    // Bedrock Dedicated Server
    if (QFileInfo::exists(dir.filePath("bedrock_server.exe")) || QFileInfo::exists(dir.filePath("bedrock_server"))) {
        s.software = "bedrock";
        s.launchType = "exe";
        // This computer's build of the server; a folder copied from another system needs it re-downloaded.
        s.launchTarget = ServerSoftware::bedrockExecutable();
        if (!QFileInfo::exists(dir.filePath(s.launchTarget))) {
            s.problem = QObject::tr("This Bedrock server was made for another system. Create a new Bedrock server here and copy "
                                    "its worlds folder into it.");
            return s;  // not valid
        }
        s.port = hasProps ? props.intValue("server-port", 19132) : 19132;
        s.eulaAccepted = true;  // BDS has no eula.txt; its licence is agreed to when it is downloaded
        for (const auto& world : QDir(dir.filePath("worlds")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            s.worlds << world;
        }
        if (hasProps && !props.value("server-name").isEmpty()) {
            s.name = props.value("server-name");
        }
        s.valid = true;
        return s;
    }

    const QStringList jars = dir.entryList({ "*.jar" }, QDir::Files, QDir::Name);
    const QString jar = serverJar(jars);
    const QString lowerJar = jar.toLower();
    const QString history = readText(dir.filePath("version_history.json"));
    static const QRegularExpression mcInHistory("MC: ([^)\\s]+)");
    static const QRegularExpression startingVersion("Starting minecraft server version (\\S+)");

    // The latest log names the Minecraft version for every Java server type.
    if (const auto m = startingVersion.match(readText(dir.filePath("logs/latest.log"), 256 * 1024)); m.hasMatch()) {
        s.mcVersion = m.captured(1);
    }

    QStringList runArgs;
    const QString neoDir = dir.filePath("libraries/net/neoforged/neoforge");
    const QString forgeDir = dir.filePath("libraries/net/minecraftforge/forge");
    if (lowerJar.startsWith("arclight")) {
        static const QRegularExpression arclight("^arclight-(fabric|neoforge|forge)-([0-9][^-]*)-", QRegularExpression::CaseInsensitiveOption);
        const auto m = arclight.match(jar);
        s.software = "arclight-" + (m.hasMatch() ? m.captured(1).toLower() : QString("neoforge"));
        if (m.hasMatch() && s.mcVersion.isEmpty()) {
            s.mcVersion = m.captured(2);
        }
    } else if (QDir(neoDir).exists()) {
        s.software = "neoforge";
        if (s.mcVersion.isEmpty()) {
            s.mcVersion = mcForNeoForge(firstSubdir(neoDir));
        }
    } else if (QDir(forgeDir).exists()) {
        s.software = "forge";
        if (s.mcVersion.isEmpty()) {
            s.mcVersion = firstSubdir(forgeDir).section('-', 0, 0);
        }
    } else if (lowerJar.startsWith("fabric-server") || QDir(dir.filePath(".fabric")).exists()) {
        s.software = "fabric";
        static const QRegularExpression fabricJar("mc\\.([^-]+)-loader");
        if (const auto m = fabricJar.match(jar); m.hasMatch() && s.mcVersion.isEmpty()) {
            s.mcVersion = m.captured(1);
        }
    } else if (lowerJar.startsWith("purpur") || history.contains("Purpur")) {
        s.software = "purpur";
    } else if (lowerJar.startsWith("folia") || history.contains("Folia")) {
        s.software = "folia";
    } else if (lowerJar.startsWith("paper") || !history.isEmpty()) {
        s.software = "paper";
    } else if (lowerJar.startsWith("spigot") || lowerJar.startsWith("craftbukkit")) {
        s.software = "paper";
        s.softwareNote = QObject::tr("Spigot/Bukkit, managed like Paper for plugins");
    } else if (!jar.isEmpty()) {
        s.software = s.plugins > 0 ? "paper" : "vanilla";
    }

    if (s.mcVersion.isEmpty()) {
        if (const auto m = mcInHistory.match(history); m.hasMatch()) {
            s.mcVersion = m.captured(1);
        } else {
            // paper-1.21.8-60.jar, purpur-1.21.8-2490.jar, minecraft_server.1.20.1.jar
            static const QRegularExpression versionInName("[-.]((?:1\\.)?\\d+\\.\\d+(?:\\.\\d+)?)(?:[-.]|$)");
            if (const auto m2 = versionInName.match(QFileInfo(jar).completeBaseName() + '.'); m2.hasMatch()) {
                s.mcVersion = m2.captured(1);
            }
        }
    }

    // How to start it: a modern Forge/NeoForge run script, else the server jar.
    if ((s.software == "forge" || s.software == "neoforge") && readRunScript(s.dir, &runArgs)) {
        s.launchType = "args";
        s.launchTarget = runArgs.join('\n');
    } else if (!jar.isEmpty()) {
        s.launchType = "jar";
        s.launchTarget = jar;
    }

    s.port = hasProps ? props.intValue("server-port", 25565) : 25565;
    for (const auto& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFileInfo::exists(dir.filePath(sub + "/level.dat"))) {
            s.worlds << sub;
            if (worldInUse(dir.filePath(sub))) {
                s.running = true;
            }
        }
    }

    if (s.launchType.isEmpty()) {
        s.problem = hasProps || !s.worlds.isEmpty()
                        ? QObject::tr("This looks like a server, but EMC could not find its server .jar or start script.")
                        : QObject::tr("This folder does not look like a Minecraft server. Pick the folder that has server.properties "
                                      "and the server .jar in it.");
        return s;
    }
    s.valid = true;
    return s;
}

ServerImportTask::ServerImportTask(ServerList* list, ServerScan scan, QString name, bool copy, bool acceptEula, QObject* parent)
    : ServerStepTask(parent), m_list(list), m_scan(std::move(scan)), m_name(std::move(name)), m_copy(copy), m_acceptEula(acceptEula)
{}

void ServerImportTask::executeTask()
{
    if (!m_list || !m_scan.valid) {
        emitFailed(m_scan.problem.isEmpty() ? tr("Nothing to import.") : m_scan.problem);
        return;
    }
    m_server = m_list->add(m_list->newId(m_name));
    m_server->setName(m_name);
    if (!m_copy) {
        m_server->settings()->set("GameDir", m_scan.dir);
        configure();
        return;
    }
    setStatus(tr("Copying the server files"));
    setProgress(0, 0);
    setAbortable(false);
    const QString src = m_scan.dir;
    const QString dst = m_server->gameDir();
    connect(&m_copyWatcher, &QFutureWatcher<bool>::finished, this, [this] {
        setAbortable(true);
        if (!m_copyWatcher.result()) {
            emitFailed(tr("Could not copy the server folder."));
            return;
        }
        configure();
    });
    m_copyWatcher.setFuture(QtConcurrent::run([src, dst] {
        FS::copy copy(src, dst);
        copy.followSymlinks(false);
        return copy();
    }));
}

void ServerImportTask::configure()
{
    auto* s = m_server->settings();
    s->set("Software", m_scan.software);
    s->set("McVersion", m_scan.mcVersion);
    s->set("LaunchType", m_scan.launchType);
    s->set("LaunchTarget", m_scan.launchTarget);
    s->set("Crossplay", m_scan.crossplay);
    if (m_acceptEula && !m_scan.eulaAccepted) {
        QFile eula(FS::PathCombine(m_server->gameDir(), "eula.txt"));
        if (eula.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            eula.write("# Accepted in EMC by the server owner (https://aka.ms/MinecraftEULA)\neula=true\n");
        }
    }
    if (m_scan.software == "bedrock") {
        emitSucceeded();
        return;
    }
    resolveJava();
}

void ServerImportTask::resolveJava()
{
    auto useJava = [this](const QString& javaName, int major) {
        m_server->settings()->set("JavaName", javaName);
        m_server->settings()->set("JavaMajor", major);
        if (!ServerJavaTask::installedJava(javaName).isEmpty()) {
            emitSucceeded();
            return;
        }
        auto task = makeShared<ServerJavaTask>(javaName, major);
        runSubTask(task, [this] { emitSucceeded(); });
    };
    if (m_scan.mcVersion.isEmpty()) {
        // Unknown version: the newest Java runs every current server; it can be changed in Settings.
        useJava("java-runtime-epsilon", 25);
        return;
    }
    setStatus(tr("Checking which Java Minecraft %1 needs").arg(m_scan.mcVersion));
    fetchJson(QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"), [this, useJava](const QJsonDocument& manifest) {
        QString url;
        for (const auto v : manifest.object().value("versions").toArray()) {
            if (v.toObject().value("id").toString() == m_scan.mcVersion) {
                url = v.toObject().value("url").toString();
            }
        }
        if (url.isEmpty()) {
            useJava("java-runtime-epsilon", 25);
            return;
        }
        fetchJson(QUrl(url), [useJava](const QJsonDocument& version) {
            const auto java = version.object().value("javaVersion").toObject();
            useJava(java.value("component").toString("jre-legacy"), java.value("majorVersion").toInt(8));
        });
    });
}

bool ServerImportTask::abort()
{
    if (m_copyWatcher.isRunning()) {
        return false;
    }
    return ServerStepTask::abort();
}

void ServerImportTask::discard()
{
    if (m_server && m_list) {
        // An in-place import only removes EMC's record; a copy removes the partial copy too.
        m_list->remove(m_server, m_copy);
        m_server = nullptr;
    }
}

void ServerImportTask::emitFailed(QString reason)
{
    discard();
    ServerStepTask::emitFailed(reason);
}

void ServerImportTask::emitAborted()
{
    discard();
    ServerStepTask::emitAborted();
}
