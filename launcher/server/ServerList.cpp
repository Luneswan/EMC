// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerList.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QRegularExpression>
#include <QThread>

#include <algorithm>

#include "FileSystem.h"
#include "ServerInstance.h"
#include "settings/SettingsObject.h"

ServerList::ServerList(QString root, QObject* parent) : QAbstractListModel(parent), m_root(std::move(root))
{
    QDir().mkpath(m_root);
}

ServerList::~ServerList() = default;

void ServerList::load()
{
    beginResetModel();
    qDeleteAll(m_servers);
    m_servers.clear();
    const QDir dir(m_root);
    for (const auto& entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!QFileInfo::exists(FS::PathCombine(entry.absoluteFilePath(), "emc-server.cfg"))) {
            continue;
        }
        auto* server = new ServerInstance(entry.fileName(), entry.absoluteFilePath(), this);
        track(server);
        m_servers.append(server);
    }
    std::sort(m_servers.begin(), m_servers.end(),
              [](ServerInstance* a, ServerInstance* b) { return a->name().compare(b->name(), Qt::CaseInsensitive) < 0; });
    endResetModel();
}

void ServerList::track(ServerInstance* server)
{
    auto refresh = [this, server] {
        const int row = m_servers.indexOf(server);
        if (row >= 0) {
            emit dataChanged(index(row), index(row));
        }
    };
    connect(server, &ServerInstance::changed, this, refresh);
    connect(server, &ServerInstance::playersChanged, this, refresh);
    connect(server, &ServerInstance::statusChanged, this, [this, refresh] {
        refresh();
        emit runningCountChanged(runningCount());
    });
}

int ServerList::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_servers.size());
}

QVariant ServerList::data(const QModelIndex& index, int role) const
{
    auto* server = m_servers.value(index.row());
    if (!server) {
        return {};
    }
    switch (role) {
        case Qt::DisplayRole:
            return server->name();
        case Qt::ToolTipRole:
            return server->typeLabel();
        case ServerRole:
            return QVariant::fromValue(static_cast<QObject*>(server));
        case StatusRole:
            return int(server->status());
        case PlayersRole:
            return server->players().size();
        default:
            return {};
    }
}

ServerInstance* ServerList::find(const QString& id) const
{
    for (auto* server : m_servers) {
        if (server->id() == id) {
            return server;
        }
    }
    return nullptr;
}

QString ServerList::newId(const QString& name) const
{
    QString base = name.toLower();
    base.replace(QRegularExpression("[^a-z0-9]+"), "-");
    base.remove(QRegularExpression("^-+|-+$"));
    if (base.isEmpty()) {
        base = "server";
    }
    QString id = base;
    for (int i = 2; QFileInfo::exists(FS::PathCombine(m_root, id)); ++i) {
        id = QString("%1-%2").arg(base).arg(i);
    }
    return id;
}

ServerInstance* ServerList::add(const QString& id)
{
    const QString metaDir = FS::PathCombine(m_root, id);
    QDir().mkpath(metaDir);
    auto* server = new ServerInstance(id, metaDir, this);
    server->settings()->set("Name", server->name());  // creates emc-server.cfg so the server is found on the next load
    track(server);
    const int row = int(std::lower_bound(m_servers.begin(), m_servers.end(), server,
                                         [](ServerInstance* a, ServerInstance* b) {
                                             return a->name().compare(b->name(), Qt::CaseInsensitive) < 0;
                                         }) -
                        m_servers.begin());
    beginInsertRows({}, row, row);
    m_servers.insert(row, server);
    endInsertRows();
    emit serverAdded(server);
    return server;
}

bool ServerList::remove(ServerInstance* server, bool deleteFiles)
{
    const int row = m_servers.indexOf(server);
    if (row < 0 || server->isActive()) {
        return false;
    }
    const bool imported = !server->settings()->get("GameDir").toString().isEmpty();
    const QString metaDir = server->metaDir();
    beginRemoveRows({}, row, row);
    m_servers.removeAt(row);
    endRemoveRows();
    delete server;
    if (deleteFiles || imported) {
        // For imported servers only EMC's own record goes; the user's folder is left alone.
        FS::deletePath(metaDir);
    }
    emit runningCountChanged(runningCount());
    return true;
}

int ServerList::runningCount() const
{
    return int(std::count_if(m_servers.begin(), m_servers.end(), [](ServerInstance* s) { return s->isActive(); }));
}

void ServerList::startAutoStartServers()
{
    for (auto* server : m_servers) {
        if (server->settings()->get("AutoStart").toBool() && !server->isActive()) {
            server->start();
        }
    }
}

void ServerList::stopAll(int timeoutMs)
{
    for (auto* server : m_servers) {
        if (server->isActive()) {
            server->stop();
        }
    }
    QDeadlineTimer deadline(timeoutMs);
    while (runningCount() > 0 && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QThread::msleep(20);
    }
    for (auto* server : m_servers) {
        if (server->isActive()) {
            server->kill();
        }
    }
}
