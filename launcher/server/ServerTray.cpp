// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerTray.h"

#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QMenu>
#include <QSettings>
#include <QSystemTrayIcon>

#include "Application.h"
#include "ServerInstance.h"
#include "ServerList.h"
#include "ui/servers/ServerWindow.h"

namespace {
#ifdef Q_OS_WIN
const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#endif
const char* kRunValue = "EMC";

QString statusMark(const ServerInstance* server)
{
    switch (server->status()) {
        case ServerInstance::Status::Running:
            return QString::fromUtf8("● ");  // filled dot
        case ServerInstance::Status::Preparing:
        case ServerInstance::Status::Starting:
        case ServerInstance::Status::Stopping:
            return QString::fromUtf8("◐ ");  // half dot
        default:
            return QString::fromUtf8("○ ");  // empty dot
    }
}
}  // namespace

ServerTray::ServerTray(ServerList* servers, QObject* parent) : QObject(parent), m_servers(servers)
{
    m_menu = new QMenu();
    m_icon = new QSystemTrayIcon(APPLICATION->logo(), this);
    m_icon->setToolTip("EMC");
    m_icon->setContextMenu(m_menu);
    connect(m_menu, &QMenu::aboutToShow, this, &ServerTray::rebuildMenu);
    connect(m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            emit openRequested();
        }
    });
    connect(m_servers, &ServerList::runningCountChanged, this, [this](int running) {
        m_icon->setToolTip(running == 0 ? QString("EMC") : (running == 1 ? tr("EMC: 1 server running") : tr("EMC: %1 servers running").arg(running)));
        updateVisibility();
    });
    connect(m_servers, &QAbstractItemModel::rowsInserted, this, &ServerTray::updateVisibility);
    connect(m_servers, &QAbstractItemModel::rowsRemoved, this, &ServerTray::updateVisibility);
    rebuildMenu();
    updateVisibility();
}

ServerTray::~ServerTray()
{
    delete m_menu;
}

void ServerTray::updateVisibility()
{
    // Shown whenever there is something to control from it.
    m_icon->setVisible(QSystemTrayIcon::isSystemTrayAvailable() && (m_inBackground || m_servers->rowCount() > 0));
}

void ServerTray::rebuildMenu()
{
    // clear() keeps submenus alive (they are children of the menu), so delete them first.
    qDeleteAll(m_menu->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly));
    m_menu->clear();
    m_menu->addAction(tr("Open EMC"), this, &ServerTray::openRequested);
    if (m_servers->rowCount() > 0) {
        m_menu->addSection(tr("Servers"));
    }
    for (auto* server : m_servers->servers()) {
        auto* sub = m_menu->addMenu(statusMark(server) + server->name());
        QPointer<ServerInstance> guard(server);
        if (server->isActive()) {
            sub->addAction(tr("Stop"), this, [guard] {
                if (guard) {
                    guard->stop();
                }
            });
            sub->addAction(tr("Restart"), this, [guard] {
                if (guard) {
                    guard->restart();
                }
            });
        } else {
            sub->addAction(tr("Start"), this, [guard] {
                if (guard) {
                    guard->start();
                }
            });
        }
        sub->addAction(tr("Open Console"), this, [guard] {
            if (guard) {
                ServerWindow::open(guard, "console");
            }
        });
    }
    m_menu->addSeparator();
    auto* autostart = m_menu->addAction(tr("Start EMC with Windows"));
    autostart->setCheckable(true);
    autostart->setChecked(startWithWindows());
    connect(autostart, &QAction::toggled, this, [](bool on) { setStartWithWindows(on); });
    m_menu->addSeparator();
    m_menu->addAction(m_servers->runningCount() > 0 ? tr("Stop Servers and Quit") : tr("Quit EMC"), this, &ServerTray::quitRequested);
}

void ServerTray::mainWindowClosed()
{
    if (m_inBackground || m_servers->runningCount() > 0) {
        enterBackground(!m_inBackground);
    }
}

void ServerTray::enterBackground(bool notify)
{
    m_inBackground = true;
    updateVisibility();
    if (notify && m_icon->isVisible() && !APPLICATION->settings()->get("ServerTrayNoticeShown").toBool()) {
        APPLICATION->settings()->set("ServerTrayNoticeShown", true);
        m_icon->showMessage(tr("EMC is still running"),
                            tr("Your servers keep running here. Right-click this icon to stop them or quit."),
                            QSystemTrayIcon::Information, 6000);
    }
}

bool ServerTray::startWithWindows()
{
#ifdef Q_OS_WIN
    QSettings run(kRunKey, QSettings::NativeFormat);
    return run.contains(kRunValue);
#else
    return false;
#endif
}

void ServerTray::setStartWithWindows(bool on)
{
#ifdef Q_OS_WIN
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (on) {
        // Starts quietly in the tray, with this data folder, so autostart servers come up.
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        const QString data = QDir::toNativeSeparators(APPLICATION->dataRoot());
        run.setValue(kRunValue, QString("\"%1\" --tray --dir \"%2\"").arg(exe, data));
    } else {
        run.remove(kRunValue);
    }
#else
    Q_UNUSED(on)
#endif
}
