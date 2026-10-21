// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QAbstractListModel>
#include <QList>

class ServerInstance;

// All servers EMC knows about: one folder per server under <data>/servers/<id>/, holding
// emc-server.cfg and (for servers EMC created) the server's files in server/.
class ServerList : public QAbstractListModel {
    Q_OBJECT
   public:
    enum Roles { ServerRole = Qt::UserRole + 1, StatusRole, PlayersRole };

    explicit ServerList(QString root, QObject* parent = nullptr);
    ~ServerList() override;

    QString root() const { return m_root; }
    void load();

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    QList<ServerInstance*> servers() const { return m_servers; }
    ServerInstance* find(const QString& id) const;
    ServerInstance* at(int row) const { return m_servers.value(row); }

    // A fresh id and metadata folder for a new server, derived from its name.
    QString newId(const QString& name) const;
    ServerInstance* add(const QString& id);
    // Deletes the server; `deleteFiles` also removes its files (never for servers imported in place).
    bool remove(ServerInstance* server, bool deleteFiles);

    int runningCount() const;
    void startAutoStartServers();
    // Stops every running server and waits (up to `timeoutMs`) for them to save and exit.
    void stopAll(int timeoutMs);

   signals:
    void serverAdded(ServerInstance* server);
    void runningCountChanged(int count);

   private:
    void track(ServerInstance* server);

    QString m_root;
    QList<ServerInstance*> m_servers;
};
