// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServersView.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "Application.h"
#include "ImportServerDialog.h"
#include "NewServerDialog.h"
#include "RemoteServerUi.h"
#include "ServerWindow.h"
#include "server/RemoteServer.h"
#include "server/ServerFirewall.h"
#include "server/ServerInstance.h"
#include "server/ServerList.h"
#include "server/ServerTray.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/widgets/WrappingLabel.h"

namespace {
QString dotColor(ServerInstance::Status status)
{
    switch (status) {
        case ServerInstance::Status::Running:
            return "#32D74B";
        case ServerInstance::Status::Preparing:
        case ServerInstance::Status::Starting:
        case ServerInstance::Status::Stopping:
            return "#FFD60A";
        case ServerInstance::Status::Crashed:
            return "#FF453A";
        case ServerInstance::Status::Stopped:
            break;
    }
    return "#6E6E73";
}
}  // namespace

ServerCard::ServerCard(ServerInstance* server, QWidget* parent) : QFrame(parent), m_server(server)
{
    setObjectName("serverCard");
    setCursor(Qt::PointingHandCursor);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(18, 14, 14, 14);
    row->setSpacing(14);

    m_dot = new QLabel(this);
    m_dot->setFixedSize(10, 10);
    row->addWidget(m_dot, 0, Qt::AlignVCenter);

    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    m_name = new QLabel(this);
    m_name->setProperty("role", "cardTitle");
    m_detail = new QLabel(this);
    m_detail->setProperty("role", "body");
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detail->setCursor(Qt::IBeamCursor);
    text->addWidget(m_name);
    text->addWidget(m_detail);
    row->addLayout(text, 1);

    m_status = new QLabel(this);
    m_status->setProperty("role", "note");
    row->addWidget(m_status);

    m_toggle = new QPushButton(this);
    m_toggle->setMinimumWidth(92);
    row->addWidget(m_toggle);
    m_open = new QPushButton(tr("Open"), this);
    row->addWidget(m_open);

    m_more = new QToolButton(this);
    m_more->setText("···");
    m_more->setPopupMode(QToolButton::InstantPopup);
    m_more->setAutoRaise(true);
    auto* menu = new QMenu(m_more);
    menu->addAction(tr("Open Folder"), this, [this] {
        if (m_server) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_server->gameDir()));
        }
    });
    menu->addAction(tr("Rename…"), this, [this] {
        if (!m_server) {
            return;
        }
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, tr("Rename server"), tr("Name in EMC:"), QLineEdit::Normal, m_server->name(), &ok).trimmed();
        if (ok && !name.isEmpty()) {
            m_server->setName(name);
        }
    });
    menu->addAction(tr("Restart"), this, [this] {
        if (m_server) {
            m_server->restart();
        }
    });
    menu->addSeparator();
    menu->addAction(tr("Delete…"), this, [this] {
        if (!m_server) {
            return;
        }
        if (m_server->isActive()) {
            QMessageBox::information(this, tr("Delete server"), tr("Stop the server before deleting it."));
            return;
        }
        const bool imported = !m_server->settings()->get("GameDir").toString().isEmpty();
        const QString message = imported
                                    ? tr("Remove \"%1\" from EMC? Its folder stays where it is:\n%2").arg(m_server->name(), m_server->gameDir())
                                    : tr("Delete \"%1\" and all its worlds, plugins and mods? This cannot be undone.").arg(m_server->name());
        auto reply = CustomMessageBox::selectable(this, tr("Delete server"), message, QMessageBox::Warning,
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                         ->exec();
        if (reply == QMessageBox::Yes) {
            APPLICATION->servers()->remove(m_server, true);
        }
    });
    m_more->setMenu(menu);
    row->addWidget(m_more);

    connect(m_toggle, &QPushButton::clicked, this, [this] {
        if (!m_server) {
            return;
        }
        if (m_server->isActive()) {
            m_server->stop();
        } else {
            m_server->start();
        }
    });
    connect(m_open, &QPushButton::clicked, this, [this] { emit openRequested(m_server); });
    connect(server, &ServerInstance::statusChanged, this, &ServerCard::refresh);
    connect(server, &ServerInstance::playersChanged, this, &ServerCard::refresh);
    connect(server, &ServerInstance::changed, this, &ServerCard::refresh);
    refresh();
}

void ServerCard::refresh()
{
    if (!m_server) {
        return;
    }
    m_dot->setStyleSheet(QString("background: %1; border-radius: 5px;").arg(dotColor(m_server->status())));
    m_name->setText(m_server->name());

    QStringList parts{ m_server->typeLabel() };
    if (m_server->isBedrock()) {
        parts << tr("Bedrock port %1").arg(m_server->port());
    } else {
        parts << tr("localhost:%1").arg(m_server->port());
        if (m_server->crossplay()) {
            parts << tr("+ Bedrock");
        }
    }
    if (m_server->status() == ServerInstance::Status::Running) {
        const int n = m_server->players().size();
        parts << (n == 1 ? tr("1 player") : tr("%1 players").arg(n));
    }
    m_detail->setText(parts.join("  ·  "));
    m_status->setText(m_server->statusText());

    const bool active = m_server->isActive();
    m_toggle->setText(active ? tr("Stop") : tr("Start"));
    m_toggle->setProperty("accent", !active);
    m_toggle->style()->unpolish(m_toggle);
    m_toggle->style()->polish(m_toggle);
    m_toggle->setEnabled(m_server->status() != ServerInstance::Status::Stopping);
}

void ServerCard::mouseDoubleClickEvent(QMouseEvent* event)
{
    QFrame::mouseDoubleClickEvent(event);
    emit openRequested(m_server);
}

ServersView::ServersView(ServerList* list, QWidget* parent) : QWidget(parent), m_list(list)
{
    ServerFirewall::setAsker([](const QString&, std::function<void(bool)> answer) {
        QMessageBox box(QApplication::activeWindow());
        box.setWindowTitle(tr("Let other devices join?"));
        box.setIcon(QMessageBox::Question);
        box.setText(tr("Windows Firewall stops players on other computers from joining your servers."));
        box.setInformativeText(tr("EMC can allow its servers through now. Windows will ask you to confirm once."));
        auto* allow = box.addButton(tr("Allow"), QMessageBox::AcceptRole);
        allow->setProperty("accent", true);
        box.addButton(tr("Not Now"), QMessageBox::RejectRole);
        box.setDefaultButton(allow);
        box.exec();
        answer(box.clickedButton() == allow);
    });

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 18);
    root->setSpacing(16);

    // Header: title and a one-line summary, actions on the right.
    auto* header = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    auto* title = new QLabel(tr("Servers"), this);
    title->setProperty("role", "title");
    m_summary = new QLabel(this);
    m_summary->setProperty("role", "body");
    titles->addWidget(title);
    titles->addWidget(m_summary);
    header->addLayout(titles, 1);
    // A server the user already has: a folder on this PC, or one hosted elsewhere.
    auto* addMenu = new QMenu(this);
    addMenu->addAction(tr("Server Folder on This PC…"), this, &ServersView::importServer);
    addMenu->addAction(tr("Hosted Server or VPS…"), this, &ServersView::addHostedServer);
    auto* importButton = new QPushButton(tr("Add Existing"), this);
    importButton->setMenu(addMenu);
    auto* createButton = new QPushButton(tr("New Server"), this);
    createButton->setProperty("accent", true);
    header->addWidget(importButton, 0, Qt::AlignBottom);
    header->addWidget(createButton, 0, Qt::AlignBottom);
    root->addLayout(header);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_cards = new QWidget(m_scroll);
    m_cards->setObjectName("serverCards");
    m_cardsLayout = new QVBoxLayout(m_cards);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(10);
    m_scroll->setWidget(m_cards);
    root->addWidget(m_scroll, 1);

    // Empty state: what a server is, and the two ways to get one.
    m_empty = new QWidget(this);
    auto* emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->setSpacing(10);
    emptyLayout->addStretch(2);
    auto* logo = new QLabel(m_empty);
    logo->setPixmap(QIcon::fromTheme("server").pixmap(56, 56));
    logo->setAlignment(Qt::AlignHCenter);
    auto* emptyTitle = new QLabel(tr("Host your own world"), m_empty);
    emptyTitle->setProperty("role", "title");
    emptyTitle->setAlignment(Qt::AlignHCenter);
    auto* emptyBody = new WrappingLabel(tr("Run a server on this computer for you and your friends. Java, Bedrock or both, "
                                           "with plugins or mods. EMC sets it up and tunes it for this device. Or add a "
                                           "server you rent or run on a VPS."),
                                        m_empty);
    emptyBody->setProperty("role", "body");
    emptyBody->setAlignment(Qt::AlignHCenter);
    emptyBody->setFixedWidth(440);
    auto* emptyCreate = new QPushButton(tr("Create Server"), m_empty);
    emptyCreate->setProperty("accent", true);
    emptyCreate->setMinimumSize(220, 36);
    auto* emptyImport = new QPushButton(tr("Add a Server You Already Have"), m_empty);
    emptyImport->setMinimumSize(220, 36);
    emptyImport->setFlat(true);
    emptyImport->setMenu(addMenu);
    emptyLayout->addWidget(logo);
    emptyLayout->addSpacing(4);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyBody, 0, Qt::AlignHCenter);
    emptyLayout->addSpacing(10);
    emptyLayout->addWidget(emptyCreate, 0, Qt::AlignHCenter);
    emptyLayout->addWidget(emptyImport, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(3);
    root->addWidget(m_empty, 1);

    // Background: servers keep running in the tray, and EMC can start with Windows.
    m_footer = new QWidget(this);
    auto* footer = new QHBoxLayout(m_footer);
    footer->setContentsMargins(0, 0, 0, 0);
    auto* startWithWindows = new QCheckBox(tr("Start EMC with Windows"), m_footer);
    startWithWindows->setChecked(ServerTray::startWithWindows());
    auto* footerNote = new QLabel(tr("Servers set to \"Start when EMC starts\" then come up on their own. Closing EMC while a "
                                     "server runs keeps it running in the tray."),
                                  m_footer);
    footerNote->setProperty("role", "note");
    footerNote->setWordWrap(true);
    footer->addWidget(startWithWindows);
    footer->addSpacing(12);
    footer->addWidget(footerNote, 1);
    root->addWidget(m_footer);
    connect(startWithWindows, &QCheckBox::toggled, this, [](bool on) { ServerTray::setStartWithWindows(on); });

    connect(createButton, &QPushButton::clicked, this, &ServersView::createServer);
    connect(emptyCreate, &QPushButton::clicked, this, &ServersView::createServer);
    connect(RemoteServerList::instance(), &RemoteServerList::listChanged, this, &ServersView::rebuild, Qt::QueuedConnection);
    // Hosted servers: refresh whether they are up while this view is on screen.
    auto* pingTimer = new QTimer(this);
    connect(pingTimer, &QTimer::timeout, this, [this] {
        if (isVisible()) {
            RemoteServerList::instance()->pingAll();
        }
    });
    pingTimer->start(30000);
    connect(m_list, &QAbstractItemModel::rowsInserted, this, &ServersView::rebuild, Qt::QueuedConnection);
    connect(m_list, &QAbstractItemModel::rowsRemoved, this, &ServersView::rebuild, Qt::QueuedConnection);
    connect(m_list, &QAbstractItemModel::modelReset, this, &ServersView::rebuild, Qt::QueuedConnection);
    connect(m_list, &ServerList::runningCountChanged, this, &ServersView::updateSummary);
    rebuild();
}

void ServersView::rebuild()
{
    while (auto* item = m_cardsLayout->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();  // a card may be removed from its own menu
        }
        delete item;
    }
    for (auto* server : m_list->servers()) {
        auto* card = new ServerCard(server, m_cards);
        connect(card, &ServerCard::openRequested, this, &ServersView::openServer);
        m_cardsLayout->addWidget(card);
    }
    const auto hosted = RemoteServerList::instance()->servers();
    if (!hosted.isEmpty()) {
        auto* heading = new QLabel(tr("Hosted elsewhere"), m_cards);
        heading->setProperty("role", "cardTitle");
        if (m_list->rowCount() > 0) {
            m_cardsLayout->addSpacing(8);
        }
        m_cardsLayout->addWidget(heading);
        for (auto* server : hosted) {
            m_cardsLayout->addWidget(new RemoteServerCard(server, m_cards));
        }
        RemoteServerList::instance()->pingAll();
    }
    m_cardsLayout->addStretch(1);
    const bool empty = m_list->rowCount() == 0 && hosted.isEmpty();
    m_scroll->setVisible(!empty);
    m_empty->setVisible(empty);
    m_footer->setVisible(m_list->rowCount() > 0);
    updateSummary();
}

void ServersView::updateSummary()
{
    const int total = m_list->rowCount();
    const int running = m_list->runningCount();
    const int hosted = int(RemoteServerList::instance()->servers().size());
    QString text;
    if (total == 0) {
        text = hosted == 0 ? tr("No servers yet") : QString();
    } else if (running == 0) {
        text = total == 1 ? tr("1 server, none running") : tr("%1 servers, none running").arg(total);
    } else {
        text = tr("%1 of %2 running").arg(running).arg(total);
    }
    if (hosted > 0) {
        const QString part = hosted == 1 ? tr("1 hosted elsewhere") : tr("%1 hosted elsewhere").arg(hosted);
        text = text.isEmpty() ? part : text + "  ·  " + part;
    }
    m_summary->setText(text);
}

void ServersView::createServer()
{
    NewServerDialog dialog(m_list, window());
    if (dialog.exec() == QDialog::Accepted && dialog.createdServer()) {
        openServer(dialog.createdServer());
    }
}

void ServersView::importServer()
{
    ImportServerDialog dialog(m_list, window());
    if (dialog.exec() == QDialog::Accepted && dialog.importedServer()) {
        openServer(dialog.importedServer());
    }
}

void ServersView::addHostedServer()
{
    RemoteServerDialog dialog(nullptr, window());
    if (dialog.exec() == QDialog::Accepted && dialog.server()) {
        RemoteServerWindow::open(dialog.server());
    }
}

void ServersView::openServer(ServerInstance* server)
{
    if (server) {
        ServerWindow::open(server);
    }
}
