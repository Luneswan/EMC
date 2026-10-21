// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QTcpSocket;

// A server that runs somewhere else: rented from a host, or on a VPS. EMC shows whether it is
// up and who is on it (the same ping the game's server list uses), runs commands over RCON when
// the user sets it up, and links to the host's control panel.
class RemoteServer : public QObject {
    Q_OBJECT
   public:
    struct Status {
        bool checked = false;
        bool online = false;
        QString motd;
        QString version;
        int players = 0;
        int maxPlayers = 0;
        QStringList sample;  // some of the players' names, when the server shares them
        int latencyMs = 0;
    };

    explicit RemoteServer(const QJsonObject& json, QObject* parent = nullptr);
    QJsonObject toJson() const;

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString host() const { return m_host; }
    int port() const { return m_port; }
    bool isBedrock() const { return m_bedrock; }
    int rconPort() const { return m_rconPort; }
    QString rconPassword() const { return m_rconPassword; }
    QString panelUrl() const { return m_panelUrl; }
    bool hasRcon() const { return !m_bedrock && !m_rconPassword.isEmpty(); }

    // host, or host:port when the port is not the edition's default
    QString address() const;
    // The Minecraft version from the last ping, like "1.21.1", or empty.
    QString mcVersion() const;
    Status status() const { return m_status; }

    void update(const QJsonObject& json);
    void ping();

   signals:
    void changed();
    void statusChanged();

   private:
    void pingJava(const QString& host, int port);
    void pingBedrock();
    void finish(const Status& status);

    QString m_id;
    QString m_name;
    QString m_host;
    int m_port = 25565;
    bool m_bedrock = false;
    int m_rconPort = 25575;
    QString m_rconPassword;
    QString m_panelUrl;
    Status m_status;
    bool m_pinging = false;
};

// Every hosted server EMC knows, kept in <data>/servers/remote.json.
class RemoteServerList : public QObject {
    Q_OBJECT
   public:
    static RemoteServerList* instance();

    QList<RemoteServer*> servers() const { return m_servers; }
    RemoteServer* add(const QJsonObject& json);
    void remove(RemoteServer* server);
    void save() const;
    void pingAll();

   signals:
    void listChanged();

   private:
    explicit RemoteServerList(QObject* parent);
    QString path() const;

    QList<RemoteServer*> m_servers;
};

// A Source RCON connection: log in once, then send commands and get their output.
class RconClient : public QObject {
    Q_OBJECT
   public:
    explicit RconClient(QObject* parent = nullptr);
    void open(const QString& host, int port, const QString& password);
    void close();
    bool isReady() const { return m_ready; }
    void send(const QString& command);

   signals:
    void ready();
    void failed(const QString& reason);
    void output(const QString& command, const QString& text);

   private:
    void write(int id, int type, const QByteArray& body);
    void onData();

    QTcpSocket* m_socket = nullptr;
    QByteArray m_buffer;
    QString m_password;
    bool m_ready = false;
    int m_nextId = 10;
    QHash<int, QString> m_pending;
};
