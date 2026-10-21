// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QPointer>
#include <QWidget>

#include "server/RouterCheck.h"
#include "ui/pages/BasePage.h"

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class OptionCard;
class ServerInstance;

// Making a server reachable from the internet: one choice (off, port forwarding, playit.gg), a
// connection test that recommends the right one, and the address to send to friends.
class ServerNetworkPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit ServerNetworkPage(ServerInstance* server, QWidget* parent = nullptr);
    QString id() const override { return "network"; }
    QString displayName() const override { return tr("Network"); }
    QIcon icon() const override { return QIcon::fromTheme("proxy"); }
    void openedImpl() override;

   private:
    void setMode(const QString& mode);
    void runRouterCheck();
    void showReport();
    void testFromInternet();
    void refresh();
    void refreshFirewall();
    void updateRecords();
    void checkDomain();
    void resolvePlayitTarget();
    // Where players' games must end up, for Java and for Bedrock.
    struct Endpoints {
        QString javaHost;
        int javaPort = 0;
        QString bedrockHost;
        int bedrockPort = 0;
    };
    Endpoints endpoints();

    QPointer<ServerInstance> m_server;
    QButtonGroup* m_modes;
    OptionCard* m_off;
    OptionCard* m_upnp;
    OptionCard* m_playit;

    QLabel* m_checkResult;
    QPushButton* m_check;
    bool m_checked = false;
    bool m_checking = false;
    bool m_testing = false;
    RouterCheck::Report m_report;

    QWidget* m_playitBox;
    QLabel* m_playitState;
    QPushButton* m_link;

    QLabel* m_address;
    QPushButton* m_copy;
    QPushButton* m_test;
    QLabel* m_testResult;
    QLabel* m_manual;

    // the user's own domain
    QLineEdit* m_domain;
    QPushButton* m_domainCheck;
    QLabel* m_records;
    QLabel* m_domainResult;
    QLabel* m_freeNote;
    QWidget* m_duckBox;
    QLineEdit* m_duckName;
    QLineEdit* m_duckToken;
    QString m_playitFor;  // the playit address m_playitTarget was resolved for
    QString m_playitTarget;
    int m_playitPort = 0;

    QWidget* m_firewallBox;
    QLabel* m_firewallState;
    QPushButton* m_firewallAllow;
};
