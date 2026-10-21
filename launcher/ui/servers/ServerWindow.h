// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QMainWindow>
#include <QPointer>

#include "ui/pages/BasePageContainer.h"

class QLabel;
class QPushButton;
class PageContainer;
class ServerInstance;

// One window per server: console, overview, settings and the rest as pages, with Start/Stop below.
class ServerWindow : public QMainWindow, public BasePageContainer {
    Q_OBJECT
   public:
    // Shows the server's window, creating it the first time.
    static ServerWindow* open(ServerInstance* server, const QString& page = {});

    bool selectPage(QString pageId) override;
    BasePage* selectedPage() const override;
    BasePage* getPage(QString pageId) override;
    void refreshContainer() override;
    bool requestClose() override;

   protected:
    void closeEvent(QCloseEvent* event) override;

   private:
    explicit ServerWindow(ServerInstance* server);
    void updateButtons();

    QPointer<ServerInstance> m_server;
    PageContainer* m_container;
    QPushButton* m_play;
    QPushButton* m_toggle;
    QPushButton* m_restart;
    QPushButton* m_kill;
};
