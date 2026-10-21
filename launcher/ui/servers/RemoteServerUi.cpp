// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "RemoteServerUi.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "ServerJoin.h"
#include "server/RemoteServer.h"

namespace {
QLabel* label(const QString& text, const char* role, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setProperty("role", role);
    l->setWordWrap(true);
    return l;
}

// Form labels share one width so the sections' fields line up; a longer (translated) label may grow.
QLabel* formLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setMinimumWidth(120);
    return l;
}

QString statusLine(const RemoteServer* server)
{
    const auto status = server->status();
    if (!status.checked) {
        return QObject::tr("Checking…");
    }
    if (!status.online) {
        return QObject::tr("Not reachable");
    }
    return QObject::tr("Online  ·  %1 of %2 players").arg(status.players).arg(status.maxPlayers);
}

QString dotColor(const RemoteServer* server)
{
    const auto status = server->status();
    return !status.checked ? "#6E6E73" : status.online ? "#32D74B" : "#FF453A";
}

void openPanel(const RemoteServer* server)
{
    QString url = server->panelUrl();
    if (!url.contains("://")) {
        url.prepend("https://");
    }
    QDesktopServices::openUrl(QUrl(url));
}

void play(RemoteServer* server, QWidget* parent)
{
    ServerJoin::join(parent, server->name(), server->isBedrock(), server->host(), server->port(), server->mcVersion());
}

QHash<RemoteServer*, QPointer<RemoteServerWindow>>& windows()
{
    static QHash<RemoteServer*, QPointer<RemoteServerWindow>> map;
    return map;
}
}  // namespace

// ---------------------------------------------------------------------------------------------

RemoteServerDialog::RemoteServerDialog(RemoteServer* server, QWidget* parent) : QDialog(parent), m_server(server)
{
    setWindowTitle(server ? tr("Edit Hosted Server") : tr("Add a Hosted Server"));
    setMinimumWidth(520);
    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->addWidget(label(tr("A server you rent from a host or run on a VPS. EMC shows whether it is up and who is "
                             "on it, and joins it with one click."),
                          "body", this));

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("My server"));
    m_address = new QLineEdit(this);
    m_address->setPlaceholderText(tr("play.example.com or 203.0.113.5:25565"));
    m_edition = new QComboBox(this);
    m_edition->addItem(tr("Java Edition"), false);
    m_edition->addItem(tr("Bedrock Edition"), true);
    form->addRow(formLabel(tr("Name"), this), m_name);
    form->addRow(formLabel(tr("Address"), this), m_address);
    form->addRow(formLabel(tr("Edition"), this), m_edition);
    root->addLayout(form);

    root->addSpacing(6);
    root->addWidget(label(tr("Console (optional)"), "cardTitle", this));
    auto* rconNote = label(tr("To run commands from EMC, turn on RCON in the server's server.properties "
                              "(enable-rcon=true and a long rcon.password), restart it, and let the RCON port "
                              "through your host's firewall. RCON is not encrypted, so use a password you use nowhere else."),
                           "note", this);
    root->addWidget(rconNote);
    auto* rconForm = new QFormLayout;
    rconForm->setHorizontalSpacing(16);
    rconForm->setVerticalSpacing(10);
    m_rconPort = new QSpinBox(this);
    m_rconPort->setRange(1, 65535);
    m_rconPort->setValue(25575);
    m_rconPassword = new QLineEdit(this);
    m_rconPassword->setEchoMode(QLineEdit::Password);
    rconForm->addRow(formLabel(tr("RCON port"), this), m_rconPort);
    rconForm->addRow(formLabel(tr("RCON password"), this), m_rconPassword);
    root->addLayout(rconForm);

    root->addSpacing(6);
    root->addWidget(label(tr("Control panel (optional)"), "cardTitle", this));
    root->addWidget(label(tr("Your host's website for files, restarts and backups."), "note", this));
    m_panel = new QLineEdit(this);
    m_panel->setPlaceholderText(tr("https://panel.yourhost.com/server/…"));
    root->addWidget(m_panel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(server ? tr("Save") : tr("Add Server"));
    buttons->button(QDialogButtonBox::Save)->setProperty("accent", true);
    buttons->button(QDialogButtonBox::Save)->setMinimumWidth(130);
    root->addSpacing(6);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto syncEdition = [this, rconNote] {
        const bool bedrock = m_edition->currentData().toBool();
        rconNote->setEnabled(!bedrock);
        m_rconPort->setEnabled(!bedrock);
        m_rconPassword->setEnabled(!bedrock);
        m_rconPassword->setPlaceholderText(bedrock ? tr("Bedrock servers have no RCON") : QString());
    };
    connect(m_edition, &QComboBox::currentIndexChanged, this, syncEdition);

    if (server) {
        m_name->setText(server->name());
        m_address->setText(server->address());
        m_edition->setCurrentIndex(server->isBedrock() ? 1 : 0);
        m_rconPort->setValue(server->rconPort());
        m_rconPassword->setText(server->rconPassword());
        m_panel->setText(server->panelUrl());
    }
    syncEdition();
}

void RemoteServerDialog::accept()
{
    QString host = m_address->text().trimmed();
    for (const QString& scheme : { QString("http://"), QString("https://"), QString("minecraft://") }) {
        if (host.startsWith(scheme, Qt::CaseInsensitive)) {
            host = host.mid(scheme.size());
        }
    }
    host = host.section('/', 0, 0);
    const bool bedrock = m_edition->currentData().toBool();
    int port = bedrock ? 19132 : 25565;
    // host:port, but leave a bare IPv6 address alone
    if (const int colon = host.lastIndexOf(':'); colon > 0 && host.count(':') == 1) {
        bool ok = false;
        const int parsed = host.mid(colon + 1).toInt(&ok);
        if (!ok || parsed < 1 || parsed > 65535) {
            QMessageBox::warning(this, windowTitle(), tr("The port after \":\" must be a number from 1 to 65535."));
            return;
        }
        port = parsed;
        host = host.left(colon);
    }
    if (host.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Enter the server's address, like play.example.com."));
        m_address->setFocus();
        return;
    }
    QString name = m_name->text().trimmed();
    if (name.isEmpty()) {
        name = host;
    }
    QJsonObject json{ { "name", name },
                      { "host", host },
                      { "port", port },
                      { "bedrock", bedrock },
                      { "rconPort", m_rconPort->value() },
                      { "rconPassword", bedrock ? QString() : m_rconPassword->text() },
                      { "panelUrl", m_panel->text().trimmed() } };
    auto* list = RemoteServerList::instance();
    if (m_server) {
        json.insert("id", m_server->id());
        m_server->update(json);
        list->save();
        m_server->ping();
    } else {
        m_server = list->add(json);
    }
    QDialog::accept();
}

// ---------------------------------------------------------------------------------------------

void RemoteServerWindow::open(RemoteServer* server)
{
    auto& map = windows();
    RemoteServerWindow* window = map.value(server);
    if (!window) {
        window = new RemoteServerWindow(server);
        map.insert(server, window);
        connect(server, &QObject::destroyed, window, &QWidget::close);
        connect(window, &QObject::destroyed, qApp, [server] { windows().remove(server); });
    }
    window->show();
    window->raise();
    window->activateWindow();
}

RemoteServerWindow::RemoteServerWindow(RemoteServer* server) : QDialog(nullptr), m_server(server)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowIcon(QIcon::fromTheme("server"));
    setWindowFlag(Qt::WindowMinMaxButtonsHint, true);
    resize(760, 620);
    m_rcon = new RconClient(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(8);
    m_title = label({}, "title", this);
    m_state = label({}, "body", this);
    root->addWidget(m_title);
    root->addWidget(m_state);

    auto* addressRow = new QHBoxLayout;
    m_address = new QLabel(this);
    m_address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont mono = m_address->font();
    mono.setFamily("Consolas");
    mono.setPixelSize(14);
    m_address->setFont(mono);
    auto* copy = new QPushButton(tr("Copy"), this);
    addressRow->addWidget(m_address, 1);
    addressRow->addWidget(copy);
    root->addLayout(addressRow);
    m_motd = label({}, "note", this);
    root->addWidget(m_motd);
    m_players = label({}, "body", this);
    root->addWidget(m_players);

    root->addSpacing(8);
    root->addWidget(label(tr("Console"), "cardTitle", this));
    m_consoleNote = label({}, "note", this);
    root->addWidget(m_consoleNote);
    m_console = new QPlainTextEdit(this);
    m_console->setReadOnly(true);
    m_console->setFont(QFont("Consolas", 10));
    root->addWidget(m_console, 1);
    m_command = new QLineEdit(this);
    m_command->setPlaceholderText(tr("Type a command, like list or say hello"));
    root->addWidget(m_command);

    auto* buttons = new QHBoxLayout;
    auto* edit = new QPushButton(tr("Edit…"), this);
    auto* remove = new QPushButton(tr("Remove…"), this);
    m_panel = new QPushButton(tr("Open Control Panel"), this);
    auto* playButton = new QPushButton(tr("Play on This Server"), this);
    playButton->setProperty("accent", true);
    buttons->addWidget(edit);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(m_panel);
    buttons->addWidget(playButton);
    root->addLayout(buttons);

    connect(copy, &QPushButton::clicked, this, [this] {
        if (m_server) {
            QApplication::clipboard()->setText(m_server->address());
        }
    });
    connect(edit, &QPushButton::clicked, this, [this] {
        if (!m_server) {
            return;
        }
        RemoteServerDialog dialog(m_server, this);
        if (dialog.exec() == QDialog::Accepted) {
            connectRcon();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] { RemoteServers::remove(m_server, this); });
    connect(m_panel, &QPushButton::clicked, this, [this] {
        if (m_server) {
            openPanel(m_server);
        }
    });
    connect(playButton, &QPushButton::clicked, this, [this] {
        if (m_server) {
            play(m_server, this);
        }
    });
    connect(m_command, &QLineEdit::returnPressed, this, [this] {
        const QString command = m_command->text().trimmed().remove(QRegularExpression("^/"));
        if (command.isEmpty() || !m_rcon->isReady()) {
            return;
        }
        appendConsole("> " + command);
        m_rcon->send(command);
        m_command->clear();
    });
    connect(m_rcon, &RconClient::ready, this, [this] {
        m_consoleNote->setText(tr("Connected over RCON."));
        m_command->setEnabled(true);
        m_command->setFocus();
    });
    connect(m_rcon, &RconClient::failed, this, [this](const QString& reason) {
        m_consoleNote->setText(tr("Could not connect over RCON: %1").arg(reason));
        m_command->setEnabled(false);
    });
    connect(m_rcon, &RconClient::output, this, [this](const QString&, const QString& text) {
        appendConsole(text.isEmpty() ? tr("(done)") : text);
    });
    connect(server, &RemoteServer::statusChanged, this, &RemoteServerWindow::refresh);
    connect(server, &RemoteServer::changed, this, &RemoteServerWindow::refresh);

    // Keep the status fresh while the window is open.
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (m_server) {
            m_server->ping();
        }
    });
    timer->start(15000);
    server->ping();
    refresh();
    connectRcon();
}

void RemoteServerWindow::refresh()
{
    if (!m_server) {
        return;
    }
    const auto status = m_server->status();
    setWindowTitle(m_server->name());
    m_title->setText(m_server->name());
    QStringList state{ statusLine(m_server) };
    if (status.online) {
        if (!status.version.isEmpty()) {
            state << status.version;
        }
        state << tr("%1 ms").arg(status.latencyMs);
    }
    m_state->setText(state.join("  ·  "));
    m_address->setText(m_server->address());
    m_motd->setText(status.motd);
    m_motd->setVisible(!status.motd.isEmpty());
    if (!status.online || status.players == 0) {
        m_players->setText(status.online ? tr("Nobody is playing right now.") : QString());
    } else if (status.sample.isEmpty()) {
        m_players->setText(tr("This server does not share who is playing."));
    } else {
        m_players->setText(tr("Playing: %1").arg(status.sample.join(", ")));
    }
    m_panel->setVisible(!m_server->panelUrl().isEmpty());
}

void RemoteServerWindow::connectRcon()
{
    m_rcon->close();
    m_command->setEnabled(false);
    if (!m_server || m_server->isBedrock()) {
        m_consoleNote->setText(tr("Bedrock servers have no remote console. Use your host's control panel."));
        m_console->hide();
        m_command->hide();
        return;
    }
    m_console->show();
    m_command->show();
    if (!m_server->hasRcon()) {
        m_consoleNote->setText(tr("Add the RCON password in Edit to run commands from here."));
        return;
    }
    m_consoleNote->setText(tr("Connecting over RCON…"));
    m_rcon->open(m_server->host(), m_server->rconPort(), m_server->rconPassword());
}

void RemoteServerWindow::appendConsole(const QString& text)
{
    m_console->appendPlainText(text.trimmed());
    m_console->verticalScrollBar()->setValue(m_console->verticalScrollBar()->maximum());
}

// ---------------------------------------------------------------------------------------------

RemoteServerCard::RemoteServerCard(RemoteServer* server, QWidget* parent) : QFrame(parent), m_server(server)
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
    text->addWidget(m_name);
    text->addWidget(m_detail);
    row->addLayout(text, 1);
    m_status = new QLabel(this);
    m_status->setProperty("role", "note");
    row->addWidget(m_status);

    auto* playButton = new QPushButton(tr("Play"), this);
    playButton->setMinimumWidth(92);
    playButton->setProperty("accent", true);
    auto* openButton = new QPushButton(tr("Open"), this);
    row->addWidget(playButton);
    row->addWidget(openButton);

    auto* more = new QToolButton(this);
    more->setText("···");
    more->setPopupMode(QToolButton::InstantPopup);
    more->setAutoRaise(true);
    auto* menu = new QMenu(more);
    auto* panel = menu->addAction(tr("Open Control Panel"), this, [this] {
        if (m_server) {
            openPanel(m_server);
        }
    });
    menu->addAction(tr("Edit…"), this, [this] {
        if (m_server) {
            RemoteServerDialog(m_server, window()).exec();
        }
    });
    menu->addSeparator();
    menu->addAction(tr("Remove…"), this, [this] { RemoteServers::remove(m_server, window()); });
    connect(menu, &QMenu::aboutToShow, this, [this, panel] { panel->setVisible(m_server && !m_server->panelUrl().isEmpty()); });
    more->setMenu(menu);
    row->addWidget(more);

    connect(playButton, &QPushButton::clicked, this, [this] {
        if (m_server) {
            play(m_server, window());
        }
    });
    connect(openButton, &QPushButton::clicked, this, [this] {
        if (m_server) {
            RemoteServerWindow::open(m_server);
        }
    });
    connect(server, &RemoteServer::statusChanged, this, &RemoteServerCard::refresh);
    connect(server, &RemoteServer::changed, this, &RemoteServerCard::refresh);
    refresh();
}

void RemoteServerCard::refresh()
{
    if (!m_server) {
        return;
    }
    m_dot->setStyleSheet(QString("background: %1; border-radius: 5px;").arg(dotColor(m_server)));
    m_name->setText(m_server->name());
    QStringList parts{ m_server->isBedrock() ? tr("Hosted · Bedrock") : tr("Hosted · Java"), m_server->address() };
    if (!m_server->mcVersion().isEmpty()) {
        parts << m_server->mcVersion();
    }
    m_detail->setText(parts.join("  ·  "));
    m_status->setText(statusLine(m_server));
}

void RemoteServerCard::mouseDoubleClickEvent(QMouseEvent* event)
{
    QFrame::mouseDoubleClickEvent(event);
    if (m_server) {
        RemoteServerWindow::open(m_server);
    }
}

void RemoteServers::remove(RemoteServer* server, QWidget* parent)
{
    if (!server) {
        return;
    }
    const auto answer = QMessageBox::question(parent, QObject::tr("Remove hosted server"),
                                              QObject::tr("Remove \"%1\" from EMC? The server itself and its files stay at your host.")
                                                  .arg(server->name()));
    if (answer == QMessageBox::Yes) {
        RemoteServerList::instance()->remove(server);
    }
}
