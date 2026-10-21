// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerContent.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QtConcurrent>

#include "Application.h"
#include "FileSystem.h"
#include "net/ApiRequest.h"
#include "net/NetJob.h"
#include "ServerInstance.h"
#include "ServerSoftware.h"

namespace ServerContent {

QString contentDir(const ServerInstance* server, bool mod)
{
    const auto* info = server->softwareInfo();
    const bool useMods = info && info->mods && (mod || !info->plugins);
    return FS::PathCombine(server->gameDir(), useMods ? "mods" : "plugins");
}

bool isModLoaderSet(const QStringList& loaders)
{
    static const QStringList modLoaders = { "fabric", "forge", "neoforge", "quilt" };
    for (const auto& l : loaders) {
        if (modLoaders.contains(l)) {
            return true;
        }
    }
    return false;
}

QList<Starter> starters(const QString& softwareId)
{
    const auto* info = ServerSoftware::find(softwareId);
    if (!info) {
        return {};
    }
    QList<Starter> out;
    // Hybrid servers get the plugin and mod lists together; a project appears once (as a plugin).
    auto add = [&out](std::initializer_list<Starter> list) {
        for (const auto& starter : list) {
            if (std::none_of(out.begin(), out.end(), [&](const Starter& s) { return s.project == starter.project; })) {
                out << starter;
            }
        }
    };
    if (info->plugins) {
        add({
            { "luckperms", QObject::tr("LuckPerms"), QObject::tr("Ranks and permissions."), true },
            { "essentialsx", QObject::tr("EssentialsX"), QObject::tr("Homes, warps, kits, /tpa and 100+ everyday commands."), true },
            { "worldedit", QObject::tr("WorldEdit"), QObject::tr("Build and edit huge areas in seconds."), false },
            { "chunky", QObject::tr("Chunky"), QObject::tr("Pre-generate the world so exploring never lags."), true },
            { "spark", QObject::tr("spark"), QObject::tr("Find out what is making the server slow."), false },
            { "simple-voice-chat", QObject::tr("Simple Voice Chat"), QObject::tr("Proximity voice chat (players need the mod too)."), false },
        });
    }
    if (info->mods && info->modrinthLoaders.contains("fabric")) {
        add({
            { "fabric-api", QObject::tr("Fabric API"), QObject::tr("Required by most Fabric mods."), true },
            { "lithium", QObject::tr("Lithium"), QObject::tr("Big server performance gains, no gameplay changes."), true },
            { "ferrite-core", QObject::tr("FerriteCore"), QObject::tr("Uses much less memory."), true },
            { "chunky", QObject::tr("Chunky"), QObject::tr("Pre-generate the world so exploring never lags."), false },
            { "spark", QObject::tr("spark"), QObject::tr("Find out what is making the server slow."), false },
        });
    } else if (info->mods) {
        add({
            { "ferrite-core", QObject::tr("FerriteCore"), QObject::tr("Uses much less memory."), true },
            { "modernfix", QObject::tr("ModernFix"), QObject::tr("Faster startup and lower memory use."), true },
            { "chunky", QObject::tr("Chunky"), QObject::tr("Pre-generate the world so exploring never lags."), false },
            { "spark", QObject::tr("spark"), QObject::tr("Find out what is making the server slow."), false },
        });
    }
    return out;
}

}  // namespace ServerContent

static QString jsonList(const QStringList& values)
{
    QStringList quoted;
    for (const auto& v : values) {
        quoted << '"' + v + '"';
    }
    return '[' + quoted.join(',') + ']';
}

void ModrinthServerInstallTask::executeTask()
{
    QDir().mkpath(m_dir);
    next();
}

void ModrinthServerInstallTask::next()
{
    if (m_queue.isEmpty()) {
        if (!m_missing.isEmpty() && m_titles.isEmpty()) {
            emitFailed(tr("%1 has no version for Minecraft %2 on this server type.").arg(m_missing.join(", "), m_mcVersion));
        } else {
            emitSucceeded();
        }
        return;
    }
    const QString project = m_queue.takeFirst();
    if (m_seen.contains(project)) {
        next();
        return;
    }
    m_seen.insert(project);
    setStatus(tr("Finding %1").arg(project));

    QUrl url(QString("https://api.modrinth.com/v2/project/%1/version").arg(project));
    QUrlQuery query;
    query.addQueryItem("loaders", jsonList(m_loaders));
    query.addQueryItem("game_versions", jsonList({ m_mcVersion }));
    url.setQuery(query);
    const bool requested = m_requested.contains(project);
    fetchJson(url, [this, project, requested](const QJsonDocument& doc) {
        const auto versions = doc.array();
        if (versions.isEmpty()) {
            if (requested) {
                m_missing << project;
            }
            // A missing project should not sink the others.
            logWarning(tr("No compatible version of %1 was found.").arg(project));
            next();
            return;
        }
        // The pinned version if there is one, else a release, else the newest beta/alpha.
        QJsonObject chosen = versions.first().toObject();
        const QString pinned = m_pinned.value(project);
        bool found = false;
        for (const auto v : versions) {
            if (!pinned.isEmpty() && v.toObject().value("id").toString() == pinned) {
                chosen = v.toObject();
                found = true;
                break;
            }
        }
        for (const auto v : versions) {
            if (found) {
                break;
            }
            if (v.toObject().value("version_type").toString() == "release") {
                chosen = v.toObject();
                break;
            }
        }
        for (const auto d : chosen.value("dependencies").toArray()) {
            const auto dep = d.toObject();
            if (dep.value("dependency_type").toString() == "required" && !dep.value("project_id").toString().isEmpty()) {
                m_queue.append(dep.value("project_id").toString());
            }
        }
        const auto files = chosen.value("files").toArray();
        QJsonObject file = files.first().toObject();
        for (const auto f : files) {
            if (f.toObject().value("primary").toBool()) {
                file = f.toObject();
            }
        }
        const QString name = file.value("filename").toString();
        if (requested) {
            m_titles << chosen.value("name").toString();
        }
        const QString path = FS::PathCombine(m_dir, name);
        if (name.isEmpty() || QFileInfo::exists(path) || QFileInfo::exists(path + ".disabled")) {
            next();
            return;
        }
        download(QUrl(file.value("url").toString()), path, file.value("hashes").toObject().value("sha1").toString(),
                 QCryptographicHash::Sha1, [this, name] {
                     m_installed << name;
                     next();
                 });
    });
}

void ModrinthServerUpdateTask::executeTask()
{
    setStatus(tr("Checking plugins and mods for updates"));
    startFolder();
}

void ModrinthServerUpdateTask::startFolder()
{
    m_folder++;
    if (m_folder >= m_folders.size()) {
        emitSucceeded();
        return;
    }
    const QString dir = m_folders.at(m_folder).first;
    // Hashing a large modpack takes a while; keep it off the UI thread.
    auto* watcher = new QFutureWatcher<QHash<QString, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        watcher->deleteLater();
        if (m_aborted || !isRunning()) {
            return;
        }
        m_files = watcher->result();
        queryFolder();
    });
    watcher->setFuture(QtConcurrent::run([dir] {
        QHash<QString, QString> files;
        for (const auto& info : QDir(dir).entryInfoList({ "*.jar" }, QDir::Files)) {
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            QCryptographicHash hash(QCryptographicHash::Sha1);
            hash.addData(&file);
            files.insert(QString::fromLatin1(hash.result().toHex()), info.absoluteFilePath());
        }
        return files;
    }));
}

void ModrinthServerUpdateTask::queryFolder()
{
    const QStringList loaders = m_folders.at(m_folder).second;
    if (m_files.isEmpty() || loaders.isEmpty()) {
        startFolder();
        return;
    }
    const auto hashes = QJsonArray::fromStringList(m_files.keys());
    const auto loaderList = QJsonArray::fromStringList(loaders);
    post("/version_files", QJsonObject{ { "hashes", hashes }, { "algorithm", "sha1" } }, [this, hashes, loaderList](const QJsonObject& current) {
        m_current = current;
        const QJsonObject body{ { "hashes", hashes },
                                { "algorithm", "sha1" },
                                { "loaders", loaderList },
                                { "game_versions", QJsonArray{ m_mcVersion } },
                                { "version_types", QJsonArray{ "release" } } };
        post("/version_files/update", body, [this](const QJsonObject& latest) {
            m_latest = latest;
            m_queue = m_latest.keys();
            next();
        });
    });
}

void ModrinthServerUpdateTask::post(const QString& path, const QJsonObject& body, std::function<void(const QJsonObject&)> next)
{
    auto job = makeShared<NetJob>(QString("EMC::ModrinthUpdate"), APPLICATION->network());
    auto [request, data] = Net::ApiRequest::makeByteArray(QUrl("https://api.modrinth.com/v2" + path), QJsonDocument(body).toJson());
    job->addNetAction(request);
    job->setAskRetry(false);
    runSubTask(job, [next, data] { next(QJsonDocument::fromJson(*data).object()); });
}

void ModrinthServerUpdateTask::next()
{
    if (m_queue.isEmpty()) {
        startFolder();
        return;
    }
    const QString oldHash = m_queue.takeFirst();
    const QString oldPath = m_files.value(oldHash);
    const auto latest = m_latest.value(oldHash).toObject();
    const auto installed = m_current.value(oldHash).toObject();
    if (oldPath.isEmpty() || latest.isEmpty() || latest.value("id") == installed.value("id")) {
        next();
        return;
    }
    // Only move forward in time: an installed beta newer than the latest release stays.
    const auto published = [](const QJsonObject& v) { return QDateTime::fromString(v.value("date_published").toString(), Qt::ISODate); };
    if (installed.isEmpty() || published(latest) <= published(installed)) {
        next();
        return;
    }
    // Same kind of file: a Bukkit plugin is never swapped for the project's Fabric mod.
    bool sameLoader = false;
    const auto had = installed.value("loaders").toArray();
    for (const auto l : latest.value("loaders").toArray()) {
        sameLoader |= had.contains(l);
    }
    if (!sameLoader) {
        next();
        return;
    }
    const auto files = latest.value("files").toArray();
    if (files.isEmpty()) {
        next();
        return;
    }
    QJsonObject file = files.first().toObject();
    for (const auto f : files) {
        if (f.toObject().value("primary").toBool()) {
            file = f.toObject();
        }
    }
    const QString name = file.value("filename").toString();
    const QString newHash = file.value("hashes").toObject().value("sha1").toString();
    if (name.isEmpty() || newHash == oldHash) {
        next();
        return;
    }
    const QString dir = QFileInfo(oldPath).absolutePath();
    const QString part = FS::PathCombine(dir, name + ".part");
    setStatus(tr("Updating %1").arg(name));
    download(QUrl(file.value("url").toString()), part, newHash, QCryptographicHash::Sha1, [this, oldPath, part, dir, name] {
        // Swap only once the new file is complete and verified, and keep the old one until the new
        // one is in place: antivirus can hold a fresh file for a moment and make the rename fail.
        const QString target = FS::PathCombine(dir, name);
        const QString backup = oldPath + ".old";
        QFile::remove(backup);
        if (!QFile::rename(oldPath, backup)) {
            QFile::remove(part);
            next();
            return;
        }
        if (target != oldPath) {
            QFile::remove(target);
        }
        if (QFile::rename(part, target)) {
            QFile::remove(backup);
            m_updated << name;
        } else {
            QFile::remove(part);
            QFile::rename(backup, oldPath);
        }
        next();
    });
}
