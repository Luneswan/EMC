// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerWindow.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QApplication>
#include <QHash>
#include <QPushButton>
#include <QStyle>

#include "Application.h"
#include "ServerContentPage.h"
#include "ServerJoin.h"
#include "ServerNetworkPage.h"
#include "ServerPages.h"
#include "server/ServerInstance.h"
#include "ui/pages/BasePageProvider.h"
#include "ui/widgets/PageContainer.h"

namespace {
QHash<ServerInstance*, QPointer<ServerWindow>>& windows()
{
    static QHash<ServerInstance*, QPointer<ServerWindow>> map;
    return map;
}
}  // namespace

ServerWindow* ServerWindow::open(ServerInstance* server, const QString& page)
{
    auto& map = windows();
    ServerWindow* window = map.value(server);
    if (!window) {
        window = new ServerWindow(server);
        map.insert(server, window);
        connect(server, &QObject::destroyed, window, &QWidget::close);
        connect(window, &QObject::destroyed, qApp, [server] { windows().remove(server); });
    }
    if (!page.isEmpty()) {
        window->selectPage(page);
    }
    window->show();
    window->setWindowState(window->windowState() & ~Qt::WindowMinimized);
    window->raise();
    window->activateWindow();
    return window;
}

ServerWindow::ServerWindow(ServerInstance* server) : QMainWindow(nullptr), m_server(server)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowIcon(QIcon::fromTheme("server"));
    setWindowTitle(server->name());
    resize(900, 640);

    GenericPageProvider provider(server->name());
    provider.addPageCreator([server] { return new ServerOverviewPage(server); });
    provider.addPageCreator([server] { return new ServerConsolePage(server); });
    provider.addPageCreator([server] { return new ServerSettingsPage(server); });
    provider.addPageCreator([server] { return new ServerContentPage(server, false); });
    provider.addPageCreator([server] { return new ServerContentPage(server, true); });
    provider.addPageCreator([server] { return new ServerNetworkPage(server); });
    provider.addPageCreator([server] { return new ServerPerformancePage(server); });
    m_container = new PageContainer(&provider, "overview", this);
    m_container->setParentContainer(this);
    setCentralWidget(m_container);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 6, 6);
    m_play = new QPushButton(tr("Play on This Server"), this);
    buttons->addWidget(m_play);
    buttons->addStretch(1);
    m_kill = new QPushButton(tr("Force Stop"), this);
    m_kill->setToolTip(tr("Ends the server immediately without saving. Use only if it stopped responding."));
    m_restart = new QPushButton(tr("Restart"), this);
    m_toggle = new QPushButton(this);
    m_toggle->setMinimumWidth(110);
    auto* close = new QPushButton(tr("Close"), this);
    buttons->addWidget(m_kill);
    buttons->addWidget(m_restart);
    buttons->addWidget(m_toggle);
    buttons->addWidget(close);
    m_container->addButtons(buttons);

    connect(m_toggle, &QPushButton::clicked, this, [this] {
        if (!m_server) {
            return;
        }
        if (m_server->isActive()) {
            m_server->stop();
        } else {
            m_server->start();
            m_container->selectPage("console");
        }
    });
    connect(m_restart, &QPushButton::clicked, this, [this] {
        if (m_server) {
            m_server->restart();
        }
    });
    connect(m_kill, &QPushButton::clicked, this, [this] {
        if (m_server) {
            m_server->kill();
        }
    });
    connect(m_play, &QPushButton::clicked, this, [this] { ServerJoin::play(m_server, this); });
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    connect(server, &ServerInstance::statusChanged, this, &ServerWindow::updateButtons);
    connect(server, &ServerInstance::changed, this, [this] {
        if (m_server) {
            setWindowTitle(m_server->name());
        }
    });
    updateButtons();
}

void ServerWindow::updateButtons()
{
    if (!m_server) {
        return;
    }
    const auto status = m_server->status();
    const bool active = m_server->isActive();
    m_toggle->setText(active ? tr("Stop") : tr("Start"));
    m_toggle->setProperty("accent", !active);
    m_toggle->style()->unpolish(m_toggle);
    m_toggle->style()->polish(m_toggle);
    m_toggle->setEnabled(status != ServerInstance::Status::Stopping);
    m_restart->setVisible(status == ServerInstance::Status::Running);
    m_play->setEnabled(status == ServerInstance::Status::Running);
    m_play->setToolTip(status == ServerInstance::Status::Running ? QString() : tr("Start the server first"));
    m_kill->setVisible(status == ServerInstance::Status::Stopping || status == ServerInstance::Status::Starting);
}

bool ServerWindow::selectPage(QString pageId)
{
    return m_container->selectPage(pageId);
}

BasePage* ServerWindow::selectedPage() const
{
    return m_container->selectedPage();
}

BasePage* ServerWindow::getPage(QString pageId)
{
    return m_container->getPage(pageId);
}

void ServerWindow::refreshContainer()
{
    m_container->refreshContainer();
}

bool ServerWindow::requestClose()
{
    if (m_container->prepareToClose()) {
        close();
        return true;
    }
    return false;
}

void ServerWindow::closeEvent(QCloseEvent* event)
{
    // Closing the window never stops the server; it keeps running in EMC.
    if (!m_container->prepareToClose()) {
        event->ignore();
        return;
    }
    event->accept();
}
