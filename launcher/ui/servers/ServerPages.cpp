// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerPages.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include "Application.h"
#include "launch/LogModel.h"
#include "server/ServerInstance.h"
#include "server/ServerNetwork.h"
#include "server/ServerOptimizer.h"
#include "server/ServerProperties.h"
#include "ui/pages/instance/LogPage.h"
#include "ui/widgets/LogView.h"

// ---- Console -------------------------------------------------------------------------------------

ServerConsolePage::ServerConsolePage(ServerInstance* server, QWidget* parent) : QWidget(parent), m_server(server)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    m_proxy = new LogFormatProxyModel(this);
    bool ok = false;
    int size = APPLICATION->settings()->get("ConsoleFontSize").toInt(&ok);
    m_proxy->setFont(QFont(APPLICATION->settings()->get("ConsoleFont").toString(), ok ? size : 11));
    m_proxy->setSourceModel(server->log());

    m_view = new LogView(this);
    m_view->setModel(m_proxy);
    m_view->setWordWrap(true);
    root->addWidget(m_view, 1);

    auto* row = new QHBoxLayout;
    m_command = new QLineEdit(this);
    m_command->setPlaceholderText(tr("Type a command, like  say hello  or  op YourName"));
    m_command->installEventFilter(this);
    m_send = new QPushButton(tr("Send"), this);
    row->addWidget(m_command, 1);
    row->addWidget(m_send);
    root->addLayout(row);

    connect(m_command, &QLineEdit::returnPressed, this, &ServerConsolePage::send);
    connect(m_send, &QPushButton::clicked, this, &ServerConsolePage::send);
    connect(server, &ServerInstance::statusChanged, this, &ServerConsolePage::updateState);
    updateState();
}

void ServerConsolePage::send()
{
    const QString command = m_command->text().trimmed();
    if (command.isEmpty() || !m_server) {
        return;
    }
    m_server->sendCommand(command);
    if (m_history.isEmpty() || m_history.last() != command) {
        m_history << command;
    }
    m_historyIndex = int(m_history.size());
    m_command->clear();
    m_view->scrollToBottom();
}

bool ServerConsolePage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_command && event->type() == QEvent::KeyPress && !m_history.isEmpty()) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            m_historyIndex = std::clamp(m_historyIndex + (key == Qt::Key_Up ? -1 : 1), 0, int(m_history.size()));
            m_command->setText(m_history.value(m_historyIndex));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ServerConsolePage::updateState()
{
    const bool running = m_server && (m_server->status() == ServerInstance::Status::Running ||
                                      m_server->status() == ServerInstance::Status::Starting);
    m_command->setEnabled(running);
    m_send->setEnabled(running);
    m_command->setPlaceholderText(running ? tr("Type a command, like  say hello  or  op YourName")
                                          : tr("Start the server to send commands"));
}

// ---- Overview ------------------------------------------------------------------------------------

namespace {
QLabel* sectionTitle(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "cardTitle");
    return label;
}

QString duration(qint64 secs)
{
    if (secs < 60) {
        return QObject::tr("%1 s").arg(secs);
    }
    if (secs < 3600) {
        return QObject::tr("%1 min").arg(secs / 60);
    }
    return QObject::tr("%1 h %2 min").arg(secs / 3600).arg((secs % 3600) / 60);
}
}  // namespace

QWidget* ServerOverviewPage::addressRow(const QString& label, QLabel** value, QPushButton** copy)
{
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* name = new QLabel(label, row);
    name->setProperty("role", "body");
    name->setMinimumWidth(150);
    *value = new QLabel(row);
    (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont mono = (*value)->font();
    mono.setFamily("Consolas");
    mono.setPixelSize(13);
    (*value)->setFont(mono);
    *copy = new QPushButton(tr("Copy"), row);
    (*copy)->setFlat(true);
    layout->addWidget(name);
    layout->addWidget(*value, 1);
    layout->addWidget(*copy);
    QLabel* target = *value;
    // "copyText" holds just the address when the row shows more than one thing.
    connect(*copy, &QPushButton::clicked, this, [target] {
        const QString copyText = target->property("copyText").toString();
        QGuiApplication::clipboard()->setText(copyText.isEmpty() ? target->text() : copyText);
    });
    return row;
}

ServerOverviewPage::ServerOverviewPage(ServerInstance* server, QWidget* parent) : QWidget(parent), m_server(server)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 4, 6, 4);
    root->setSpacing(8);

    m_status = new QLabel(this);
    m_status->setProperty("role", "title");
    m_type = new QLabel(this);
    m_type->setProperty("role", "body");
    root->addWidget(m_status);
    root->addWidget(m_type);
    root->addSpacing(14);

    root->addWidget(sectionTitle(tr("How to join"), this));
    root->addWidget(addressRow(tr("On this computer"), &m_local, &m_copyLocal));
    root->addWidget(addressRow(tr("Same Wi-Fi or network"), &m_lan, &m_copyLan));
    m_bedrockRow = addressRow(tr("Bedrock players"), &m_bedrock, &m_copyBedrock);
    root->addWidget(m_bedrockRow);
    m_publicRow = addressRow(tr("Over the internet"), &m_public, &m_copyPublic);
    root->addWidget(m_publicRow);
    m_internetNote = new QLabel(this);
    m_internetNote->setProperty("role", "note");
    m_internetNote->setWordWrap(true);
    root->addWidget(m_internetNote);
    root->addSpacing(14);

    m_playersTitle = sectionTitle(tr("Players"), this);
    root->addWidget(m_playersTitle);
    m_players = new QListWidget(this);
    m_players->setContextMenuPolicy(Qt::CustomContextMenu);
    m_players->setMaximumHeight(160);
    root->addWidget(m_players);
    auto* hint = new QLabel(tr("Right-click a player to make them an operator, kick or ban them."), this);
    hint->setProperty("role", "note");
    root->addWidget(hint);
    root->addSpacing(14);

    root->addWidget(sectionTitle(tr("Running"), this));
    m_uptime = new QLabel(this);
    m_uptime->setProperty("role", "body");
    m_memory = new QLabel(this);
    m_memory->setProperty("role", "body");
    root->addWidget(m_uptime);
    root->addWidget(m_memory);
    root->addStretch(1);

    connect(m_players, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = m_players->itemAt(pos);
        if (!item || !m_server) {
            return;
        }
        const QString name = item->text();
        QMenu menu(this);
        menu.addAction(tr("Make operator"), this, [this, name] { m_server->sendCommand("op " + name); });
        menu.addAction(tr("Remove operator"), this, [this, name] { m_server->sendCommand("deop " + name); });
        menu.addSeparator();
        menu.addAction(tr("Kick"), this, [this, name] { m_server->sendCommand("kick " + name); });
        if (!m_server->isBedrock()) {
            menu.addAction(tr("Ban"), this, [this, name] { m_server->sendCommand("ban " + name); });
        }
        menu.exec(m_players->viewport()->mapToGlobal(pos));
    });
    connect(server, &ServerInstance::statusChanged, this, &ServerOverviewPage::refresh);
    connect(server, &ServerInstance::playersChanged, this, &ServerOverviewPage::refresh);
    connect(server, &ServerInstance::changed, this, &ServerOverviewPage::refresh);
    connect(&m_ticker, &QTimer::timeout, this, &ServerOverviewPage::refresh);
    m_ticker.start(5000);
    refresh();
}

void ServerOverviewPage::refresh()
{
    if (!m_server) {
        return;
    }
    m_status->setText(m_server->statusText());
    m_type->setText(m_server->typeLabel());

    const int port = m_server->port();
    const QString lan = ServerNetwork::lanAddress();
    const bool defaultPort = (m_server->isBedrock() && port == 19132) || (!m_server->isBedrock() && port == 25565);
    m_local->setText(defaultPort && !m_server->isBedrock() ? QString("localhost") : QString("localhost:%1").arg(port));
    m_lan->setText(lan.isEmpty() ? tr("Not connected to a network") : (defaultPort && !m_server->isBedrock() ? lan : QString("%1:%2").arg(lan).arg(port)));
    m_copyLan->setEnabled(!lan.isEmpty());
    m_bedrockRow->setVisible(m_server->crossplay() && !m_server->isBedrock());
    const QString bedrockHost = lan.isEmpty() ? QString("localhost") : lan;
    m_bedrock->setText(QString("%1  port %2").arg(bedrockHost).arg(m_server->bedrockPort()));
    m_bedrock->setProperty("copyText", bedrockHost);  // Bedrock asks for the address and the port separately

    const QStringList internet = m_server->publicAddresses();
    m_publicRow->setVisible(!internet.isEmpty());
    m_public->setText(internet.join("   "));
    m_public->setProperty("copyText", internet.value(0));
    if (m_server->publicMode() == "off") {
        m_internetNote->setText(tr("Friends somewhere else can join once the server is made public in the Network page."));
    } else if (internet.isEmpty()) {
        m_internetNote->setText(tr("The internet address appears here once the server starts."));
    } else {
        m_internetNote->setText(tr("Send this address to friends. It works while the server runs."));
    }

    const auto players = m_server->players();
    m_playersTitle->setText(players.isEmpty() ? tr("Players") : tr("Players (%1)").arg(players.size()));
    m_players->clear();
    if (players.isEmpty()) {
        auto* item = new QListWidgetItem(m_server->status() == ServerInstance::Status::Running ? tr("Nobody online") : tr("Server is not running"));
        item->setFlags(Qt::NoItemFlags);
        m_players->addItem(item);
    } else {
        m_players->addItems(players);
    }

    const bool running = m_server->isActive() && m_server->startedAt().isValid();
    m_uptime->setText(running ? tr("Up for %1").arg(duration(m_server->startedAt().secsTo(QDateTime::currentDateTime())))
                              : tr("Not running"));
    m_memory->setText(m_server->isBedrock() ? tr("Bedrock manages its own memory")
                                            : tr("Up to %1 MB of memory").arg(m_server->effectiveMaxMemMiB()));
}

// ---- Settings ------------------------------------------------------------------------------------

namespace {
QLabel* savedNote(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setProperty("role", "note");
    return label;
}

QString restartHint(const ServerInstance* server)
{
    return server && server->isActive() ? QObject::tr("Saved. Restart the server to use the new settings.") : QObject::tr("Saved.");
}

QString gigabytes(int mib)
{
    return QObject::tr("%1 GB").arg(mib / 1024.0, 0, 'f', mib % 1024 ? 1 : 0);
}
}  // namespace

ServerSettingsPage::ServerSettingsPage(ServerInstance* server, QWidget* parent) : QWidget(parent), m_server(server)
{
    const bool bedrock = server->isBedrock();
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 4, 6, 4);
    root->setSpacing(10);

    m_form = new QWidget(this);
    auto* form = new QFormLayout(m_form);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    m_name = new QLineEdit(m_form);
    m_name->setMaxLength(48);
    m_motd = new QLineEdit(m_form);
    m_motd->setMaxLength(bedrock ? 48 : 59);
    m_gamemode = new QComboBox(m_form);
    m_gamemode->addItem(tr("Survival"), "survival");
    m_gamemode->addItem(tr("Creative"), "creative");
    m_gamemode->addItem(tr("Adventure"), "adventure");
    if (!bedrock) {
        m_gamemode->addItem(tr("Spectator"), "spectator");
    }
    m_difficulty = new QComboBox(m_form);
    m_difficulty->addItem(tr("Peaceful"), "peaceful");
    m_difficulty->addItem(tr("Easy"), "easy");
    m_difficulty->addItem(tr("Normal"), "normal");
    m_difficulty->addItem(tr("Hard"), "hard");
    m_maxPlayers = new QSpinBox(m_form);
    m_maxPlayers->setRange(1, 1000);
    m_port = new QSpinBox(m_form);
    m_port->setRange(1024, 65535);
    form->addRow(tr("Name in EMC"), m_name);
    form->addRow(bedrock ? tr("Server name") : tr("Description"), m_motd);
    form->addRow(tr("Game mode"), m_gamemode);
    form->addRow(tr("Difficulty"), m_difficulty);
    form->addRow(tr("Player limit"), m_maxPlayers);
    form->addRow(tr("Port"), m_port);
    root->addWidget(m_form);

    m_pvp = new QCheckBox(tr("Players can hurt each other"), this);
    m_whitelist = new QCheckBox(tr("Only players I allow can join (whitelist)"), this);
    m_offlinePlayers = new QCheckBox(tr("Let players without a Microsoft account join"), this);
    m_flight = new QCheckBox(tr("Allow flying (needed by some plugins and mods)"), this);
    m_commandBlocks = new QCheckBox(tr("Allow command blocks"), this);
    m_cheats = new QCheckBox(tr("Allow cheats"), this);
    for (auto* box : { m_pvp, m_whitelist, m_offlinePlayers, m_flight, m_commandBlocks, m_cheats }) {
        root->addWidget(box);
    }
    m_pvp->setVisible(!bedrock);
    m_offlinePlayers->setVisible(!bedrock);
    m_flight->setVisible(!bedrock);
    m_commandBlocks->setVisible(!bedrock);
    m_cheats->setVisible(bedrock);

    root->addSpacing(10);
    auto* startup = new QLabel(tr("Starting"), this);
    startup->setProperty("role", "cardTitle");
    root->addWidget(startup);
    m_autoStart = new QCheckBox(tr("Start this server when EMC starts"), this);
    m_restartOnCrash = new QCheckBox(tr("Restart it automatically if it crashes"), this);
    root->addWidget(m_autoStart);
    root->addWidget(m_restartOnCrash);

    root->addSpacing(10);
    auto* files = new QLabel(tr("Files"), this);
    files->setProperty("role", "cardTitle");
    root->addWidget(files);
    auto* filesRow = new QHBoxLayout;
    auto* where = new QLabel(tr("Worlds, plugins, mods and settings are all in this folder. Copy it to back up or move the "
                                "server; renaming here does not change it.<br><b>%1</b>")
                                 .arg(QDir::toNativeSeparators(server->gameDir()).toHtmlEscaped()),
                             this);
    where->setProperty("role", "note");
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* openFolder = new QPushButton(tr("Open Folder"), this);
    connect(openFolder, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_server ? m_server->gameDir() : QString())); });
    filesRow->addWidget(where, 1);
    filesRow->addWidget(openFolder, 0, Qt::AlignTop);
    root->addLayout(filesRow);

    m_raw = new QPlainTextEdit(this);
    m_raw->setFont(QFont("Consolas", 10));
    m_raw->hide();
    root->addWidget(m_raw, 1);
    root->addStretch(0);

    auto* buttons = new QHBoxLayout;
    m_rawToggle = new QPushButton(tr("Edit All Properties…"), this);
    m_rawToggle->setFlat(true);
    m_saved = savedNote(this);
    m_save = new QPushButton(tr("Save"), this);
    m_save->setProperty("accent", true);
    buttons->addWidget(m_rawToggle);
    buttons->addStretch(1);
    buttons->addWidget(m_saved);
    buttons->addWidget(m_save);
    root->addLayout(buttons);

    auto markDirty = [this] {
        if (!m_loading) {
            m_save->setEnabled(true);
            m_saved->clear();
        }
    };
    for (auto* edit : { m_name, m_motd }) {
        connect(edit, &QLineEdit::textEdited, this, markDirty);
    }
    for (auto* combo : { m_gamemode, m_difficulty }) {
        connect(combo, &QComboBox::currentIndexChanged, this, markDirty);
    }
    for (auto* spin : { m_maxPlayers, m_port }) {
        connect(spin, &QSpinBox::valueChanged, this, markDirty);
    }
    for (auto* box : { m_pvp, m_whitelist, m_offlinePlayers, m_flight, m_commandBlocks, m_cheats, m_autoStart, m_restartOnCrash }) {
        connect(box, &QCheckBox::toggled, this, markDirty);
    }
    connect(m_raw, &QPlainTextEdit::textChanged, this, markDirty);
    connect(m_save, &QPushButton::clicked, this, &ServerSettingsPage::save);
    connect(m_rawToggle, &QPushButton::clicked, this, &ServerSettingsPage::toggleRaw);
    load();
}

QString ServerSettingsPage::key(const QString& javaKey) const
{
    if (m_server && m_server->isBedrock()) {
        if (javaKey == "white-list") {
            return "allow-list";
        }
        if (javaKey == "motd") {
            return "server-name";
        }
    }
    return javaKey;
}

void ServerSettingsPage::load()
{
    if (!m_server) {
        return;
    }
    m_loading = true;
    ServerProperties props;
    props.load(m_server->propertiesPath());
    auto setCombo = [](QComboBox* combo, const QString& value) {
        const int i = combo->findData(value.toLower());
        if (i >= 0) {
            combo->setCurrentIndex(i);
        }
    };
    m_name->setText(m_server->name());
    m_motd->setText(props.value(key("motd")));
    setCombo(m_gamemode, props.value("gamemode", "survival"));
    setCombo(m_difficulty, props.value("difficulty", "easy"));
    m_maxPlayers->setValue(props.intValue("max-players", 20));
    m_port->setValue(m_server->port());
    m_pvp->setChecked(props.boolValue("pvp", true));
    m_whitelist->setChecked(props.boolValue(key("white-list"), false));
    m_offlinePlayers->setChecked(!props.boolValue("online-mode", true));
    m_flight->setChecked(props.boolValue("allow-flight", false));
    m_commandBlocks->setChecked(props.boolValue("enable-command-block", false));
    m_cheats->setChecked(props.boolValue("allow-cheats", false));
    m_autoStart->setChecked(m_server->settings()->get("AutoStart").toBool());
    m_restartOnCrash->setChecked(m_server->settings()->get("RestartOnCrash").toBool());
    m_raw->setPlainText(props.toText());
    m_save->setEnabled(false);
    m_loading = false;
}

void ServerSettingsPage::save()
{
    if (!m_server) {
        return;
    }
    ServerProperties props;
    props.setUnicodeEscapes(!m_server->isBedrock());
    if (!m_raw->isHidden()) {  // not isVisible: this also runs when the page is not on screen
        // The raw editor wins: it is what the user is looking at.
        props.parse(m_raw->toPlainText());
    } else {
        props.load(m_server->propertiesPath());
        props.setValue(key("motd"), m_motd->text().trimmed());
        props.setValue("gamemode", m_gamemode->currentData().toString());
        props.setValue("difficulty", m_difficulty->currentData().toString());
        props.setValue("max-players", m_maxPlayers->value());
        props.setValue("server-port", m_port->value());
        props.setValue(key("white-list"), m_whitelist->isChecked());
        if (m_server->isBedrock()) {
            props.setValue("allow-cheats", m_cheats->isChecked());
        } else {
            props.setValue("pvp", m_pvp->isChecked());
            props.setValue("online-mode", !m_offlinePlayers->isChecked());
            props.setValue("allow-flight", m_flight->isChecked());
            props.setValue("enable-command-block", m_commandBlocks->isChecked());
            props.setValue("enforce-whitelist", m_whitelist->isChecked());
        }
    }
    if (!props.save(m_server->propertiesPath())) {
        m_saved->setText(tr("Could not save server.properties."));
        return;
    }
    if (!m_name->text().trimmed().isEmpty()) {
        m_server->setName(m_name->text().trimmed());
    }
    m_server->settings()->set("AutoStart", m_autoStart->isChecked());
    m_server->settings()->set("RestartOnCrash", m_restartOnCrash->isChecked());
    load();
    m_saved->setText(restartHint(m_server));
}

void ServerSettingsPage::openedImpl()
{
    if (!m_save->isEnabled()) {
        load();  // coming back to the page keeps edits that are not saved yet
    }
}

bool ServerSettingsPage::apply()
{
    if (m_save->isEnabled()) {
        save();
    }
    return true;
}

void ServerSettingsPage::toggleRaw()
{
    const bool showRaw = m_raw->isHidden();
    if (m_save->isEnabled()) {
        save();  // switching views never loses an edit
    }
    m_raw->setVisible(showRaw);
    m_form->setVisible(!showRaw);
    for (auto* box : findChildren<QCheckBox*>()) {
        box->setEnabled(!showRaw);
    }
    m_rawToggle->setText(showRaw ? tr("Back to the Simple View") : tr("Edit All Properties…"));
}

// ---- Performance ---------------------------------------------------------------------------------

ServerPerformancePage::ServerPerformancePage(ServerInstance* server, QWidget* parent) : QWidget(parent), m_server(server)
{
    const bool bedrock = server->isBedrock();
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 4, 6, 4);
    root->setSpacing(10);

    m_auto = new QCheckBox(tr("Tune memory for this computer automatically"), this);
    m_summary = new QLabel(this);
    m_summary->setProperty("role", "note");
    m_summary->setWordWrap(true);
    root->addWidget(m_auto);
    root->addWidget(m_summary);

    m_manual = new QWidget(this);
    auto* form = new QFormLayout(m_manual);
    form->setContentsMargins(0, 6, 0, 0);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    auto* memoryBox = new QWidget(m_manual);
    auto* memoryRow = new QHBoxLayout(memoryBox);
    memoryRow->setContentsMargins(0, 0, 0, 0);
    m_memory = new QSlider(Qt::Horizontal, memoryBox);
    const auto machine = ServerOptimizer::Machine::current();
    m_memory->setRange(2, std::max(2, int(std::min<quint64>(machine.ramMiB - 1024, 32768) / 512)));
    m_memoryLabel = new QLabel(memoryBox);
    m_memoryLabel->setMinimumWidth(60);
    memoryRow->addWidget(m_memory, 1);
    memoryRow->addWidget(m_memoryLabel);
    form->addRow(tr("Memory"), memoryBox);
    m_view = new QSpinBox(m_manual);
    m_view->setRange(bedrock ? 5 : 2, bedrock ? 96 : 32);
    m_view->setSuffix(tr(" chunks"));
    m_simulation = new QSpinBox(m_manual);
    m_simulation->setRange(bedrock ? 4 : 2, bedrock ? 12 : 32);
    m_simulation->setSuffix(tr(" chunks"));
    form->addRow(tr("View distance"), m_view);
    form->addRow(bedrock ? tr("Tick distance") : tr("Simulation distance"), m_simulation);
    m_simulationLabel = qobject_cast<QLabel*>(form->labelForField(m_simulation));
    m_aikar = new QCheckBox(tr("Use Aikar's flags (smoother garbage collection)"), m_manual);
    form->addRow(QString(), m_aikar);
    m_jvmArgs = new QLineEdit(m_manual);
    m_jvmArgs->setPlaceholderText(tr("Optional"));
    form->addRow(tr("Extra Java arguments"), m_jvmArgs);
    m_java = new QLineEdit(m_manual);
    m_java->setPlaceholderText(tr("Automatic (downloaded by EMC)"));
    form->addRow(tr("Java"), m_java);
    root->addWidget(m_manual);
    if (bedrock) {
        m_auto->hide();
        for (QWidget* w : std::initializer_list<QWidget*>{ memoryBox, m_aikar, m_jvmArgs, m_java }) {
            w->hide();
            if (auto* label = form->labelForField(w)) {
                label->hide();
            }
        }
    }
    root->addStretch(1);

    auto* buttons = new QHBoxLayout;
    auto* retuneButton = new QPushButton(tr("Re-tune for This Computer"), this);
    retuneButton->setFlat(true);
    m_saved = savedNote(this);
    m_save = new QPushButton(tr("Save"), this);
    m_save->setProperty("accent", true);
    buttons->addWidget(retuneButton);
    buttons->addStretch(1);
    buttons->addWidget(m_saved);
    buttons->addWidget(m_save);
    root->addLayout(buttons);

    auto markDirty = [this] {
        if (!m_loading) {
            m_save->setEnabled(true);
            m_saved->clear();
        }
    };
    connect(m_auto, &QCheckBox::toggled, this, [this, markDirty] {
        updateEnabled();
        markDirty();
    });
    connect(m_memory, &QSlider::valueChanged, this, [this, markDirty](int v) {
        m_memoryLabel->setText(gigabytes(v * 512));
        markDirty();
    });
    connect(m_view, &QSpinBox::valueChanged, this, markDirty);
    connect(m_simulation, &QSpinBox::valueChanged, this, markDirty);
    connect(m_aikar, &QCheckBox::toggled, this, markDirty);
    connect(m_jvmArgs, &QLineEdit::textEdited, this, markDirty);
    connect(m_java, &QLineEdit::textEdited, this, markDirty);
    connect(m_save, &QPushButton::clicked, this, &ServerPerformancePage::save);
    connect(retuneButton, &QPushButton::clicked, this, &ServerPerformancePage::retune);
    load();
}

void ServerPerformancePage::load()
{
    if (!m_server) {
        return;
    }
    m_loading = true;
    auto* s = m_server->settings();
    const auto* info = m_server->softwareInfo();
    const auto plan = ServerOptimizer::plan(ServerOptimizer::Machine::current(), info && info->mods, m_server->isBedrock());
    m_summary->setText(plan.summary);
    m_auto->setChecked(s->get("MemoryAuto").toBool());
    m_memory->setValue(m_server->effectiveMaxMemMiB() / 512);
    m_memoryLabel->setText(gigabytes(m_memory->value() * 512));
    m_aikar->setChecked(s->get("AikarFlags").toBool());
    m_jvmArgs->setText(s->get("JvmArgs").toString());
    m_java->setText(s->get("JavaPath").toString());
    ServerProperties props;
    props.load(m_server->propertiesPath());
    const bool bedrock = m_server->isBedrock();
    m_view->setValue(props.intValue("view-distance", bedrock ? 32 : 10));
    m_simulation->setValue(props.intValue(bedrock ? "tick-distance" : "simulation-distance", bedrock ? 4 : 10));
    updateEnabled();
    m_save->setEnabled(false);
    m_loading = false;
}

void ServerPerformancePage::updateEnabled()
{
    const bool manual = !m_auto->isChecked();
    m_memory->setEnabled(manual);
    m_aikar->setEnabled(manual);
}

void ServerPerformancePage::save()
{
    if (!m_server) {
        return;
    }
    auto* s = m_server->settings();
    s->set("MemoryAuto", m_auto->isChecked());
    if (!m_auto->isChecked()) {
        const int max = m_memory->value() * 512;
        s->set("MaxMemMiB", max);
        s->set("MinMemMiB", std::min(max, std::max(1024, max / 2)));
        s->set("AikarFlags", m_aikar->isChecked());
    } else {
        s->set("AikarFlags", true);
    }
    s->set("JvmArgs", m_jvmArgs->text().trimmed());
    s->set("JavaPath", m_java->text().trimmed());
    ServerProperties props;
    props.setUnicodeEscapes(!m_server->isBedrock());
    props.load(m_server->propertiesPath());
    props.setValue("view-distance", m_view->value());
    props.setValue(m_server->isBedrock() ? "tick-distance" : "simulation-distance", m_simulation->value());
    if (!props.save(m_server->propertiesPath())) {
        m_saved->setText(tr("Could not save server.properties."));
        return;
    }
    emit m_server->changed();
    load();
    m_saved->setText(restartHint(m_server));
}

void ServerPerformancePage::retune()
{
    if (!m_server) {
        return;
    }
    const auto* info = m_server->softwareInfo();
    const auto plan = ServerOptimizer::plan(ServerOptimizer::Machine::current(), info && info->mods, m_server->isBedrock());
    ServerProperties props;
    props.setUnicodeEscapes(!m_server->isBedrock());
    props.load(m_server->propertiesPath());
    ServerOptimizer::apply(plan, props, m_server->isBedrock());
    props.save(m_server->propertiesPath());
    m_server->settings()->set("MemoryAuto", true);
    m_server->settings()->set("AikarFlags", true);
    load();
    m_saved->setText(restartHint(m_server));
}

void ServerPerformancePage::openedImpl()
{
    if (!m_save->isEnabled()) {
        load();  // coming back to the page keeps edits that are not saved yet
    }
}

bool ServerPerformancePage::apply()
{
    if (m_save->isEnabled()) {
        save();
    }
    return true;
}
