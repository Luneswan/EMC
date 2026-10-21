// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ContentAutoUpdate.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>

#include "Application.h"
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"
#include "net/Mode.h"
#include "tasks/ConcurrentTask.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/ResourceUpdateDialog.h"

namespace {
// Modrinth does not change by the minute; checking every few hours keeps launches quick.
constexpr qint64 checkEverySecs = 6 * 60 * 60;
}  // namespace

void ContentAutoUpdate::updateInstance(QWidget* parent, MinecraftInstance* instance)
{
    auto settings = APPLICATION->settings();
    if (!settings->get("AutoUpdateContent").toBool() || settings->get("ModMetadataDisabled").toBool()) {
        return;
    }
    auto* profile = instance->getPackProfile();
    if (!profile) {
        return;
    }
    // At launch the components may not be read yet; read them from disk.
    if (profile->getComponentVersion("net.minecraft").isEmpty()) {
        profile->reload(Net::Mode::Offline);
        if (auto task = profile->getCurrentTask(); task && task->isRunning()) {
            QEventLoop loop;
            QObject::connect(task.get(), &Task::finished, &loop, &QEventLoop::quit);
            loop.exec();
        }
    }
    if (!profile->getModLoaders().has_value()) {
        return;
    }

    // The marker's age says when this instance was last checked.
    const QString marker = FS::PathCombine(instance->instanceRoot(), ".emc-updates-checked");
    const QFileInfo markerInfo(marker);
    if (markerInfo.exists() && markerInfo.lastModified().secsTo(QDateTime::currentDateTime()) < checkEverySecs) {
        return;
    }
    auto touchMarker = [&marker] {
        QFile file(marker);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
        }
    };

    auto* model = instance->loaderModList();
    {
        QEventLoop loop;
        QObject::connect(model, &ResourceFolderModel::updateFinished, &loop, &QEventLoop::quit);
        model->update();
        if (model->isUpdating()) {  // also when a scan was already running: wait for it, not a stale list
            loop.exec();
        }
    }
    QList<Resource*> mods;
    for (auto* mod : model->allResources()) {
        if (mod->enabled()) {
            mods.append(mod);
        }
    }
    if (mods.isEmpty()) {
        touchMarker();
        return;
    }

    ResourceUpdateDialog check(parent, instance, model, mods, true, profile->getModLoadersList());
    check.setQuiet(true);
    check.checkCandidates();
    if (check.aborted()) {
        return;  // offline or skipped: try again next launch
    }
    touchMarker();
    if (check.noUpdates()) {
        return;
    }

    ConcurrentTask downloads(QObject::tr("Updating mods"), settings->get("NumberOfConcurrentDownloads").toInt());
    for (const auto& task : check.getTasks()) {
        downloads.addTask(task);
    }
    ProgressDialog progress(parent);
    progress.setWindowTitle(QObject::tr("Updating mods"));
    progress.showSkipButton();
    progress.execWithTask(&downloads);
    qDebug() << "Updated mods of" << instance->name() << ":" << check.getTasks().size();
    model->update();
}
