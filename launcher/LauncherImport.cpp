// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "LauncherImport.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QtConcurrent>

#include <optional>


#include "Application.h"
#include "BuildConfig.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "ui/dialogs/ProgressDialog.h"

// After the launcher headers: windows.h macros clash with some of their names.
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

const QString ledgerName = "imported-launchers.json";

// Paths are compared as Windows compares them.
QString key(const QString& path)
{
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    return clean.toLower();
#else
    return clean;
#endif
}

std::optional<LauncherImport::Source> probe(const QString& path)
{
    static const QList<QPair<QString, QString>> kinds = {
        { "prismlauncher.cfg", "Prism Launcher" }, { "polymc.cfg", "PolyMC" }, { "ultimmc.cfg", "UltimMC" }, { "multimc.cfg", "MultiMC" }
    };
    const QDir dir(path);
    if (!dir.exists() || dir.exists(BuildConfig.LAUNCHER_CONFIGFILE)) {
        return std::nullopt;  // missing, or EMC's own data
    }
    for (const auto& [cfg, name] : kinds) {
        if (!dir.exists(cfg)) {
            continue;
        }
        const QSettings settings(dir.filePath(cfg), QSettings::IniFormat);
        const QString instances = dir.absoluteFilePath(settings.value("InstanceDir", "instances").toString());
        if (!QDir(instances).exists()) {
            continue;
        }
        // UltimMC is a MultiMC fork and may keep MultiMC's config name; its program gives it away.
        const bool ultim = dir.exists("UltimMC.exe") || dir.exists("UltimMC");
        return LauncherImport::Source{ ultim ? "UltimMC" : name, dir.absolutePath(), QDir(instances).absolutePath() };
    }
    return std::nullopt;
}

QString readInstanceName(const QString& instanceDir)
{
    return QSettings(FS::PathCombine(instanceDir, "instance.cfg"), QSettings::IniFormat).value("name").toString();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The same instance is already in EMC: same folder, same name, same components.
bool alreadyPresent(const QString& source, const QString& emcInstances)
{
    const QString target = FS::PathCombine(emcInstances, QFileInfo(source).fileName());
    return QFileInfo::exists(FS::PathCombine(target, "instance.cfg")) && readInstanceName(target) == readInstanceName(source) &&
           readFile(FS::PathCombine(target, "mmc-pack.json")) == readFile(FS::PathCombine(source, "mmc-pack.json"));
}

QList<QString> instanceDirs(const LauncherImport::Source& source)
{
    QList<QString> dirs;
    for (const auto& info : QDir(source.instancesDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!info.fileName().startsWith('.') && QFileInfo::exists(info.absoluteFilePath() + "/instance.cfg")) {
            dirs << info.absoluteFilePath();
        }
    }
    return dirs;
}

QSet<QString> readLedger(const QString& dataDir)
{
    QSet<QString> ledger;
    const auto doc = QJsonDocument::fromJson(readFile(FS::PathCombine(dataDir, ledgerName)));
    for (const auto v : doc.object().value("imported").toArray()) {
        ledger.insert(v.toString());
    }
    return ledger;
}

void writeLedger(const QString& dataDir, const QSet<QString>& ledger)
{
    QStringList list(ledger.begin(), ledger.end());
    list.sort();
    QJsonObject root{ { "imported", QJsonArray::fromStringList(list) } };
    FS::write(FS::PathCombine(dataDir, ledgerName), QJsonDocument(root).toJson());
}

QString emcInstancesDir()
{
    return QDir(APPLICATION->dataRoot()).absoluteFilePath(APPLICATION->settings()->get("InstanceDir").toString());
}

// Copies the files of `from` that `to` does not have yet.
void copyMissing(const QString& from, const QString& to)
{
    if (!QDir(from).exists()) {
        return;
    }
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir base(from);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = FS::PathCombine(to, base.relativeFilePath(file));
        if (QFileInfo::exists(target)) {
            continue;
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::copy(file, target);
    }
}

// Instances keep an absolute path to the Java their launcher downloaded; that Java was copied too.
void repointJava(const QString& instanceDir, const QString& oldData, const QString& newData)
{
    const QString cfgPath = FS::PathCombine(instanceDir, "instance.cfg");
    const QString oldJava = QDir::fromNativeSeparators(QDir(oldData).absoluteFilePath("java")) + '/';
    const QString newJava = QDir::fromNativeSeparators(QDir(newData).absoluteFilePath("java")) + '/';
    QStringList lines = QString::fromUtf8(readFile(cfgPath)).split('\n');
    bool changed = false;
    for (auto& line : lines) {
        if (line.startsWith("JavaPath=")) {
            const QString value = QDir::fromNativeSeparators(line.mid(9));
            if (value.startsWith(oldJava, Qt::CaseInsensitive)) {
                line = "JavaPath=" + newJava + value.mid(oldJava.size());
                changed = true;
            }
        }
    }
    if (changed) {
        FS::write(cfgPath, lines.join('\n').toUtf8());
    }
}

// Instance id -> group name, from a launcher's instgroups.json.
QHash<QString, QString> readGroups(const QString& sourceInstances)
{
    QHash<QString, QString> groups;
    const auto doc = QJsonDocument::fromJson(readFile(FS::PathCombine(sourceInstances, "instgroups.json")));
    const auto all = doc.object().value("groups").toObject();
    for (auto it = all.begin(); it != all.end(); ++it) {
        for (const auto id : it.value().toObject().value("instances").toArray()) {
            groups.insert(id.toString(), it.key());
        }
    }
    return groups;
}

QString accountKey(const QJsonObject& account)
{
    const auto profile = account.value("profile").toObject();
    QString id = profile.value("id").toString();
    if (id.isEmpty()) {
        id = profile.value("name").toString();
    }
    if (id.isEmpty()) {
        id = QString::fromUtf8(QJsonDocument(account).toJson(QJsonDocument::Compact));
    }
    return account.value("type").toString() + ':' + id;
}

// Adds the accounts EMC does not have. Returns how many were added.
int mergeAccounts(const QStringList& files)
{
    // Through the account list, not by rewriting accounts.json: that file may be unreadable for a
    // moment (antivirus) or mid-refresh, and rewriting it could lose accounts or fresh sign-ins.
    auto* list = APPLICATION->accounts();
    QSet<QString> known;
    for (int i = 0; i < list->count(); ++i) {
        known.insert(accountKey(list->at(i)->saveToJson()));
    }
    int added = 0;
    for (const auto& file : files) {
        for (const auto a : QJsonDocument::fromJson(readFile(file)).object().value("accounts").toArray()) {
            QJsonObject account = a.toObject();
            const QString type = account.value("type").toString();
            if (type == "Local" || type == "Offline") {
                // An offline account is only a name; rebuild it in EMC's format (UltimMC calls these "Local").
                const auto profile = account.value("profile").toObject();
                QString name = profile.value("name").toString();
                if (name.isEmpty()) {
                    name = account.value("ygg").toObject().value("extra").toObject().value("userName").toString();
                }
                if (name.isEmpty()) {
                    continue;
                }
                account = MinecraftAccount::createOffline(name)->saveToJson();
            }
            account.remove("active");  // keep the account EMC already uses
            if (known.contains(accountKey(account))) {
                continue;
            }
            auto loaded = MinecraftAccount::loadFromJsonV3(account);
            if (!loaded) {
                continue;
            }
            known.insert(accountKey(account));
            list->addAccount(loaded);
            added++;
        }
    }
    return added;
}

void finish(QWidget* parent, const LauncherImportTask::Result& result, bool reportNothing)
{
    const int accounts = mergeAccounts(result.accountFiles);
    if (!result.instances.isEmpty()) {
        auto* instances = APPLICATION->instances();
        instances->loadList();
        for (auto it = result.groups.begin(); it != result.groups.end(); ++it) {
            instances->setInstanceGroup(it.key(), it.value());
        }
    }
    QStringList lines;
    for (auto it = result.perSource.begin(); it != result.perSource.end(); ++it) {
        lines << QObject::tr("From %1: %2").arg(it.key(), it.value().join(", "));
    }
    if (accounts > 0) {
        lines << (accounts == 1 ? QObject::tr("1 account") : QObject::tr("%1 accounts").arg(accounts));
    }
    if (lines.isEmpty()) {
        if (reportNothing) {
            QMessageBox::information(parent, QObject::tr("Nothing new"),
                                     QObject::tr("Everything from that launcher is already in %1.").arg(BuildConfig.LAUNCHER_DISPLAYNAME));
        }
        return;
    }
    QMessageBox::information(parent, QObject::tr("Brought over to %1").arg(BuildConfig.LAUNCHER_DISPLAYNAME),
                             QObject::tr("These are now in %1. The other launcher keeps its own copy.\n\n%2")
                                 .arg(BuildConfig.LAUNCHER_DISPLAYNAME, lines.join('\n')));
}

void runImport(QWidget* parent, const QList<LauncherImport::Source>& sources, bool reportNothing)
{
    auto* task = new LauncherImportTask(sources, APPLICATION->dataRoot());
    ProgressDialog progress(parent);
    progress.setWindowTitle(QObject::tr("Bringing your instances over"));
    progress.execWithTask(task);
    if (task->isRunning()) {
        // Closed early (Esc): let the copy finish in the background, then report it.
        QPointer<QWidget> guard(parent);
        QObject::connect(task, &Task::finished, task, [task, guard, reportNothing] {
            finish(guard, task->result(), reportNothing);
            task->deleteLater();
        });
        return;
    }
    finish(parent, task->result(), reportNothing);
    delete task;
}

}  // namespace

QList<LauncherImport::Source> LauncherImport::detect()
{
    QList<Source> found;
    QSet<QString> seen;
    auto consider = [&](const QString& dir) {
        if (seen.contains(key(dir))) {
            return;
        }
        seen.insert(key(dir));
        if (auto source = probe(dir)) {
            found << *source;
        }
    };

    // Where the installed launchers keep their data.
#ifdef Q_OS_WIN
    const QDir appData(qEnvironmentVariable("APPDATA"));
#else
    const QDir appData(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
#endif
    for (const auto* name : { "PrismLauncher", "PolyMC", "multimc", "UltimMC" }) {
        consider(appData.absoluteFilePath(name));
    }
    const QString home = QDir::homePath();
    // Scoop on Windows; Flatpak on Linux.
    for (const auto* name : { "scoop/persist/prismlauncher", "scoop/persist/multimc", "scoop/persist/polymc",
                              ".var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher",
                              ".var/app/org.polymc.PolyMC/data/PolyMC" }) {
        consider(QDir(home).absoluteFilePath(name));
    }

    // Portable copies are unpacked where people put downloads: look two folders deep.
    QStringList roots = { QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
                          QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                          QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), home,
                          QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/Programs" };
#ifdef Q_OS_WIN
    // Other drives too (D:\Games...). On macOS and Linux portable copies live under the home folder.
    for (const auto& volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady() || volume.isReadOnly()) {
            continue;
        }
        // Local disks only: a network share or a sleeping USB drive can hang the scan.
        if (GetDriveTypeW(reinterpret_cast<const wchar_t*>(volume.rootPath().utf16())) != DRIVE_FIXED) {
            continue;
        }
        roots << volume.rootPath();
    }
#endif
    static const QSet<QString> skip = { "windows", "$recycle.bin", "system volume information", "appdata", "node_modules",
                                        "programdata", "recovery", "msys64", ".git" };
    for (const auto& root : roots) {
        if (root.isEmpty()) {
            continue;
        }
        consider(root);
        for (const auto& first : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (skip.contains(first.fileName().toLower())) {
                continue;
            }
            consider(first.absoluteFilePath());
            const auto second = QDir(first.absoluteFilePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (int i = 0; i < second.size() && i < 400; i++) {
                if (!skip.contains(second[i].fileName().toLower())) {
                    consider(second[i].absoluteFilePath());
                }
            }
        }
    }
    return found;
}

void LauncherImport::runAtStartup(QWidget* parent)
{
    if (!APPLICATION->settings()->get("AutoImportLaunchers").toBool()) {
        return;
    }
    const QString dataDir = APPLICATION->dataRoot();
    auto* watcher = new QFutureWatcher<QList<Source>>(parent);
    QObject::connect(watcher, &QFutureWatcher<QList<Source>>::finished, parent, [watcher, parent] {
        const auto sources = watcher->result();
        watcher->deleteLater();
        if (!sources.isEmpty()) {
            runImport(parent, sources, false);
        }
    });
    watcher->setFuture(QtConcurrent::run([dataDir] {
        QList<Source> fresh;
        for (const auto& source : detect()) {
            if (LauncherImportTask::hasNew(source, dataDir)) {
                fresh << source;
            }
        }
        return fresh;
    }));
}

void LauncherImport::importFromFolder(QWidget* parent)
{
    const QString dir = QFileDialog::getExistingDirectory(
        parent, QObject::tr("Choose the folder of Prism Launcher, PolyMC, MultiMC or UltimMC (the one with its instances folder)"));
    if (dir.isEmpty()) {
        return;
    }
    const auto source = probe(dir);
    if (!source) {
        QMessageBox::warning(parent, QObject::tr("No launcher found"),
                             QObject::tr("That folder has no Prism Launcher, PolyMC, MultiMC or UltimMC data. Pick the folder that "
                                         "holds its instances folder and its .cfg file."));
        return;
    }
    runImport(parent, { *source }, true);
}

LauncherImportTask::LauncherImportTask(QList<LauncherImport::Source> sources, QString dataDir)
    : m_sources(std::move(sources)), m_dataDir(std::move(dataDir))
{
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
        m_result = m_watcher.result();
        emitSucceeded();
    });
}

bool LauncherImportTask::hasNew(const LauncherImport::Source& source, const QString& dataDir)
{
    const auto ledger = readLedger(dataDir);
    const QString emcInstances =
        QDir(dataDir).absoluteFilePath(QSettings(FS::PathCombine(dataDir, BuildConfig.LAUNCHER_CONFIGFILE), QSettings::IniFormat)
                                           .value("InstanceDir", "instances")
                                           .toString());
    for (const auto& dir : instanceDirs(source)) {
        if (!ledger.contains(key(dir)) && !alreadyPresent(dir, emcInstances)) {
            return true;
        }
    }
    return false;
}

void LauncherImportTask::executeTask()
{
    setStatus(tr("Looking at your other launchers"));
    const QString instances = emcInstancesDir();
    m_watcher.setFuture(QtConcurrent::run([this, instances] {
        Result result;
        auto ledger = readLedger(m_dataDir);
        auto status = [this](const QString& text) { QMetaObject::invokeMethod(this, [this, text] { setStatus(text); }); };
        for (const auto& source : m_sources) {
            QList<QString> fresh;
            for (const auto& dir : instanceDirs(source)) {
                if (ledger.contains(key(dir))) {
                    continue;
                }
                if (alreadyPresent(dir, instances)) {
                    ledger.insert(key(dir));
                    continue;
                }
                fresh << dir;
            }
            if (QFileInfo::exists(FS::PathCombine(source.dir, "accounts.json"))) {
                result.accountFiles << FS::PathCombine(source.dir, "accounts.json");
            }
            if (fresh.isEmpty()) {
                continue;
            }
            // What the instances share: Java, game files, icons and skins. Only missing files are copied.
            for (const auto* sub : { "java", "icons", "skins", "themes", "meta", "libraries", "assets" }) {
                status(tr("Copying %1 from %2").arg(sub, source.name));
                copyMissing(FS::PathCombine(source.dir, sub), FS::PathCombine(m_dataDir, sub));
            }
            const auto groups = readGroups(source.instancesDir);
            for (const auto& dir : fresh) {
                const QString id = QFileInfo(dir).fileName();
                QString targetId = id;
                for (int n = 1; QFileInfo::exists(FS::PathCombine(instances, targetId)); n++) {
                    targetId = n == 1 ? QString("%1 (%2)").arg(id, source.name) : QString("%1 (%2) %3").arg(id, source.name).arg(n);
                }
                const QString name = readInstanceName(dir).isEmpty() ? id : readInstanceName(dir);
                status(tr("Copying %1 from %2").arg(name, source.name));
                // Copied into a staging folder first, so EMC never lists a half-copied instance, and a
                // failed copy (disk full, a file the game holds open) is tried again next time.
                const QString target = FS::PathCombine(instances, targetId);
                const QString staging = FS::PathCombine(m_dataDir, ".import-staging", targetId);
                QDir(staging).removeRecursively();
                QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                bool ok = QDir().mkpath(staging);
                while (ok && it.hasNext()) {
                    const QString file = it.next();
                    const QString to = FS::PathCombine(staging, QDir(dir).relativeFilePath(file));
                    ok = QDir().mkpath(QFileInfo(to).absolutePath()) && QFile::copy(file, to);
                }
                ok = ok && QDir().rename(staging, target);
                if (!ok) {
                    qWarning() << "Could not copy" << dir << "into EMC; it will be tried again next time";
                    QDir(staging).removeRecursively();
                    continue;
                }
                repointJava(target, source.dir, m_dataDir);
                result.instances << targetId;
                result.perSource[source.name] << name;
                if (groups.contains(id)) {
                    result.groups.insert(targetId, groups.value(id));
                }
                ledger.insert(key(dir));
            }
        }
        writeLedger(m_dataDir, ledger);
        return result;
    }));
}
