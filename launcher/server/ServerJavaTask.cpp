// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerJavaTask.h"

#include <QDir>
#include <QFileInfo>

#include "Application.h"
#include "FileSystem.h"
#include "SysInfo.h"
#include "java/JavaMetadata.h"
#include "java/download/ArchiveDownloadTask.h"
#include "java/download/ManifestDownloadTask.h"
#include "java/download/SymlinkTask.h"
#include "tasks/SequentialTask.h"
#include "meta/Index.h"
#include "meta/Version.h"
#include "meta/VersionList.h"
#include "minecraft/VersionFile.h"

QString ServerJavaTask::installedJava(const QString& javaName)
{
    if (javaName.isEmpty()) {
        return {};
    }
    const QDir javaDir(APPLICATION->javaPath());
#ifdef Q_OS_WIN
    // java.exe, not javaw.exe: a server is a console program.
    const QString exe = "java.exe";
#else
    const QString exe = "java";
#endif
    const QString path = javaDir.absoluteFilePath(FS::PathCombine(javaName, "bin", exe));
    if (QFileInfo::exists(path)) {
        return path;
    }
#ifdef Q_OS_MACOS
    // Mojang's macOS runtimes are bundles.
    const QString bundled = javaDir.absoluteFilePath(FS::PathCombine(javaName, "jre.bundle/Contents/Home/bin", exe));
    if (QFileInfo::exists(bundled)) {
        return bundled;
    }
#endif
    return {};
}

void ServerJavaTask::executeTask()
{
    m_path = installedJava(m_javaName);
    if (!m_path.isEmpty()) {
        emitSucceeded();
        return;
    }
    setStatus(tr("Getting Java %1").arg(m_major));
    const QString metaVersion = QString("java%1").arg(m_major);
    auto load = APPLICATION->metadataIndex()->loadVersion("net.minecraft.java", metaVersion, Net::Mode::Online);
    runSubTask(load, [this, metaVersion] { downloadFrom(metaVersion); });
}

void ServerJavaTask::downloadFrom(const QString& metaVersion)
{
    auto version = APPLICATION->metadataIndex()->get("net.minecraft.java", metaVersion);
    if (!version || !version->data()) {
        emitFailed(tr("No Java %1 is available for this computer.").arg(m_major));
        return;
    }
    const QString arch = SysInfo::getSupportedJavaArchitecture();
    // Mojang's runtime by its exact name, else any build of this Java version for this computer
    // (Mojang publishes some only under other names on ARM Linux and Apple Silicon).
    Java::MetadataPtr pick;
    for (const auto& java : version->data()->runtimes) {
        if (java->runtimeOS == arch && (java->name() == m_javaName || !pick)) {
            pick = java;
        }
    }
    if (pick) {
        const auto& java = pick;
        // Saved under the name the server asks for, so later starts find it.
        const QString finalPath = QDir(APPLICATION->javaPath()).absoluteFilePath(m_javaName.isEmpty() ? java->m_name : m_javaName);
        Task::Ptr task;
        if (java->downloadType == Java::DownloadType::Manifest) {
            task = makeShared<Java::ManifestDownloadTask>(java->url, finalPath, java->checksumType, java->checksumHash);
        } else if (java->downloadType == Java::DownloadType::Archive) {
            task = makeShared<Java::ArchiveDownloadTask>(java->url, finalPath, java->checksumType, java->checksumHash);
        } else {
            emitFailed(tr("No Java %1 build was found for %2.").arg(m_major).arg(arch));
            return;
        }
#ifdef Q_OS_MACOS
        // macOS runtimes keep java under Contents/Home; link bin/ next to it, as the game's Java does.
        auto seq = makeShared<SequentialTask>(tr("Install Java"));
        seq->addTask(task);
        seq->addTask(makeShared<Java::SymlinkTask>(finalPath));
        task = seq;
#endif
        setStatus(tr("Downloading Java %1").arg(m_major));
        runSubTask(task, [this] {
            m_path = installedJava(m_javaName);
            if (m_path.isEmpty()) {
                emitFailed(tr("Java was downloaded but could not be found."));
            } else {
                emitSucceeded();
            }
        });
        return;
    }
    emitFailed(tr("No Java %1 build was found for %2.").arg(m_major).arg(arch));
}
