// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QFrame>
#include <QPointer>
#include <QWidget>

#include "server/ServerInstance.h"

class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class QScrollArea;
class ServerInstance;
class ServerList;

// One server as a card: status, name, type, address and players, with Start/Stop and Open.
class ServerCard : public QFrame {
    Q_OBJECT
   public:
    ServerCard(ServerInstance* server, QWidget* parent = nullptr);
    ServerInstance* server() const { return m_server; }

   signals:
    void openRequested(ServerInstance* server);

   protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

   private:
    void refresh();

    QPointer<ServerInstance> m_server;
    QLabel* m_dot;
    QLabel* m_name;
    QLabel* m_detail;
    QLabel* m_status;
    QPushButton* m_toggle;
    QPushButton* m_open;
    QToolButton* m_more;
};

// The "Servers" side of the main window: every server as a card, plus create and import.
class ServersView : public QWidget {
    Q_OBJECT
   public:
    explicit ServersView(ServerList* list, QWidget* parent = nullptr);

   public slots:
    void createServer();
    void importServer();
    void addHostedServer();
    void openServer(ServerInstance* server);

   private:
    void rebuild();
    void updateSummary();

    ServerList* m_list;
    QLabel* m_summary;
    QScrollArea* m_scroll;
    QWidget* m_cards;
    QVBoxLayout* m_cardsLayout;
    QWidget* m_empty;
    QWidget* m_footer;
};
