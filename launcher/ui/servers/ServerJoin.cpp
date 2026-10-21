// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerJoin.h"

#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>

#include "Application.h"
#include "InstanceList.h"
#include "meta/Index.h"
#include "meta/VersionList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "server/ServerInstance.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {
const QStringList kLoaderUids{ "net.fabricmc.fabric-loader", "net.minecraftforge", "net.neoforged", "org.quiltmc.quilt-loader" };

QString loaderUidFor(const QString& software)
{
    if (software == "forge" || software == "arclight-forge") {
        return "net.minecraftforge";
    }
    if (software == "neoforge" || software == "arclight-neoforge") {
        return "net.neoforged";
    }
    return {};  // vanilla clients join vanilla, plugin and (server-side) Fabric servers
}

// The best instance for joining: same Minecraft version and, for Forge-style servers, the same loader.
MinecraftInstance* findInstance(const QString& mcVersion, const QString& loaderUid)
{
    MinecraftInstance* withLoader = nullptr;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); ++i) {
        auto* inst = list->at(i);
        auto profile = inst ? inst->getPackProfile() : nullptr;
        if (!profile || profile->getComponentVersion("net.minecraft") != mcVersion) {
            continue;
        }
        if (!loaderUid.isEmpty()) {
            if (!profile->getComponentVersion(loaderUid).isEmpty()) {
                return inst;
            }
            continue;
        }
        const bool hasLoader = std::any_of(kLoaderUids.begin(), kLoaderUids.end(),
                                           [&](const QString& uid) { return !profile->getComponentVersion(uid).isEmpty(); });
        if (!hasLoader) {
            return inst;
        }
        withLoader = withLoader ? withLoader : inst;
    }
    return withLoader;
}

MinecraftInstance* createInstance(const QString& mcVersion, QWidget* parent)
{
    auto versions = APPLICATION->metadataIndex()->get("net.minecraft");
    if (!versions->isLoaded()) {
        auto load = versions->getLoadTask();
        ProgressDialog dialog(parent);
        dialog.execWithTask(load.get());
    }
    auto version = versions->getVersion(mcVersion);
    if (!version) {
        QMessageBox::warning(parent, QObject::tr("Couldn't make an instance"),
                             versions->isLoaded() ? QObject::tr("Minecraft %1 was not found in the version list.").arg(mcVersion)
                                                  : QObject::tr("The Minecraft version list could not be loaded. Check your connection."));
        return nullptr;
    }
    QString createdId;
    auto* task = new VanillaCreationTask(version);
    task->setName(QObject::tr("Minecraft %1").arg(mcVersion));
    task->setGroup(APPLICATION->settings()->get("LastUsedGroupForNewInstance").toString());
    task->setIcon("grass");
    auto connection = QObject::connect(APPLICATION->instances(), &InstanceList::instanceSelectRequest, parent,
                                       [&createdId](QString id) { createdId = id; });
    unique_qobject_ptr<Task> wrapped(APPLICATION->instances()->wrapInstanceTask(task));
    ProgressDialog dialog(parent);
    dialog.execWithTask(wrapped.get());
    QObject::disconnect(connection);
    if (!wrapped->wasSuccessful()) {
        if (!wrapped->failReason().isEmpty()) {
            CustomMessageBox::selectable(parent, QObject::tr("Could not create the instance"), wrapped->failReason(), QMessageBox::Warning)
                ->exec();
        }
        return nullptr;
    }
    return createdId.isEmpty() ? findInstance(mcVersion, {}) : APPLICATION->instances()->getInstanceById(createdId);
}
}  // namespace

bool ServerJoin::bedrockInstalled()
{
#ifdef Q_OS_WIN
    const QString packages = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("Packages");
    return QDir(packages).exists("Microsoft.MinecraftUWP_8wekyb3d8bbwe");
#else
    return false;
#endif
}

void ServerJoin::play(ServerInstance* server, QWidget* parent)
{
    if (server) {
        join(parent, server->name(), server->isBedrock(), "localhost", server->port(), server->mcVersion(), loaderUidFor(server->software()));
    }
}

void ServerJoin::join(QWidget* parent, const QString& name, bool bedrock, const QString& host, int port, const QString& mcVersion,
                      const QString& loaderUid)
{
    const QString address = QString("%1:%2").arg(host).arg(port);
    if (bedrock) {
        if (!bedrockInstalled()) {
            QMessageBox::information(parent, QObject::tr("Bedrock is not installed"),
                                     QObject::tr("Minecraft Bedrock Edition is not installed on this PC. Get it from the Microsoft Store, "
                                                 "then add the server with address %1 and port %2.")
                                         .arg(host)
                                         .arg(port));
            return;
        }
        // Adds the server to Bedrock's Servers tab and opens the game.
        QUrl url("minecraft:");
        QUrlQuery query;
        query.addQueryItem("addExternalServer", QString("%1|%2").arg(name, address));
        url.setQuery(query);
        QDesktopServices::openUrl(url);
        return;
    }

    if (mcVersion.isEmpty()) {
        QMessageBox::information(parent, QObject::tr("Server not reachable"),
                                 QObject::tr("EMC could not reach the server to learn its Minecraft version. Check that it is "
                                             "running, or join %1 from the game's Multiplayer menu.")
                                     .arg(address));
        return;
    }
    MinecraftInstance* instance = findInstance(mcVersion, loaderUid);
    if (!instance && !loaderUid.isEmpty()) {
        QMessageBox::information(parent, QObject::tr("A modded game is needed"),
                                 QObject::tr("This server runs mods, so players need the same mod loader and mods. Create an instance "
                                             "with Minecraft %1 and the same loader, add the server's mods to it, then press Play again.")
                                     .arg(mcVersion));
        return;
    }
    if (!instance) {
        const auto answer = QMessageBox::question(
            parent, QObject::tr("Play on this server"),
            QObject::tr("You have no Minecraft %1 instance yet. Create one and join the server?").arg(mcVersion));
        if (answer != QMessageBox::Yes) {
            return;
        }
        instance = createInstance(mcVersion, parent);
        if (!instance) {
            return;
        }
    }
    if (instance->isRunning()) {
        QMessageBox::information(parent, QObject::tr("Already playing"),
                                 QObject::tr("\"%1\" is already running. Join the server from its Multiplayer menu: %2")
                                     .arg(instance->name(), address));
        return;
    }
    auto target = std::make_shared<MinecraftTarget>();
    target->address = host;
    target->port = quint16(port);
    APPLICATION->launch(instance, LaunchMode::Normal, target);
}
