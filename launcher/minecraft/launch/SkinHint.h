// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QString>

class QTcpSocket;
class MinecraftInstance;

// An offline account can wear the Ely.by skin of any name, but the game only ever tells a server the account's own
// name, so the server cannot show that skin to anyone else. Before the game starts, EMC pings each server in the
// instance's list once (the same server list ping the multiplayer screen sends) and puts a hint after the address,
// Forge-style: "<host>\0EMC-SKIN\0<player>\0<ely.by name>". A server that knows the hint dresses the player in that
// skin; any other server ignores everything after the first \0 and answers the ping as always.
class SkinHint : public QObject {
    Q_OBJECT
   public:
    // Fire and forget: each ping cleans up after itself within a few seconds and never delays the launch.
    static void sendAll(MinecraftInstance* instance, const QString& player, const QString& elyName, const QString& joinAddress);

   private:
    SkinHint(const QString& address, const QString& player, const QString& elyName);
    void start();
    void connectTo(const QString& host, quint16 port, const QString& handshakeHost);
    void finish();

    QString m_address;
    QString m_player;
    QString m_ely;
    QTcpSocket* m_socket = nullptr;
    bool m_done = false;
};
