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
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "ui/pages/BasePage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QSlider;
class QSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class LogView;
class LogFormatProxyModel;
class ServerInstance;

// The live server log, with a command line underneath.
class ServerConsolePage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit ServerConsolePage(ServerInstance* server, QWidget* parent = nullptr);
    QString id() const override { return "console"; }
    QString displayName() const override { return tr("Console"); }
    QIcon icon() const override { return QIcon::fromTheme("log"); }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void send();
    void updateState();

    QPointer<ServerInstance> m_server;
    LogView* m_view;
    LogFormatProxyModel* m_proxy;
    QLineEdit* m_command;
    QPushButton* m_send;
    QStringList m_history;
    int m_historyIndex = 0;
};

// Status at a glance: how to join, who is online, and how it is running.
class ServerOverviewPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit ServerOverviewPage(ServerInstance* server, QWidget* parent = nullptr);
    QString id() const override { return "overview"; }
    QString displayName() const override { return tr("Overview"); }
    QIcon icon() const override { return QIcon::fromTheme("status-good"); }

   private:
    void refresh();
    QWidget* addressRow(const QString& label, QLabel** value, QPushButton** copy);

    QPointer<ServerInstance> m_server;
    QLabel* m_status;
    QLabel* m_type;
    QLabel* m_local;
    QLabel* m_lan;
    QLabel* m_bedrock;
    QWidget* m_bedrockRow;
    QPushButton* m_copyLocal;
    QPushButton* m_copyLan;
    QPushButton* m_copyBedrock;
    QLabel* m_public;
    QWidget* m_publicRow;
    QPushButton* m_copyPublic;
    QLabel* m_internetNote;
    QLabel* m_playersTitle;
    QListWidget* m_players;
    QLabel* m_uptime;
    QLabel* m_memory;
    QTimer m_ticker;
};

// The everyday server.properties settings as a form, with the full file one click away, plus how
// the server starts.
class ServerSettingsPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit ServerSettingsPage(ServerInstance* server, QWidget* parent = nullptr);
    QString id() const override { return "settings"; }
    QString displayName() const override { return tr("Settings"); }
    QIcon icon() const override { return QIcon::fromTheme("settings"); }
    bool apply() override;
    void openedImpl() override;

   private:
    void load();
    void save();
    void toggleRaw();
    QString key(const QString& javaKey) const;

    QPointer<ServerInstance> m_server;
    QWidget* m_form;
    QLineEdit* m_name;
    QLineEdit* m_motd;
    QComboBox* m_gamemode;
    QComboBox* m_difficulty;
    QSpinBox* m_maxPlayers;
    QSpinBox* m_port;
    QCheckBox* m_pvp;
    QCheckBox* m_whitelist;
    QCheckBox* m_offlinePlayers;
    QCheckBox* m_flight;
    QCheckBox* m_commandBlocks;
    QCheckBox* m_cheats;
    QCheckBox* m_autoStart;
    QCheckBox* m_restartOnCrash;
    QPlainTextEdit* m_raw;
    QPushButton* m_rawToggle;
    QPushButton* m_save;
    QLabel* m_saved;
    bool m_loading = false;
};

// Memory, Java and view distance: automatic for this computer, or set by hand.
class ServerPerformancePage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit ServerPerformancePage(ServerInstance* server, QWidget* parent = nullptr);
    QString id() const override { return "performance"; }
    QString displayName() const override { return tr("Performance"); }
    QIcon icon() const override { return QIcon::fromTheme("java"); }
    bool apply() override;
    void openedImpl() override;

   private:
    void load();
    void save();
    void retune();
    void updateEnabled();

    QPointer<ServerInstance> m_server;
    QCheckBox* m_auto;
    QLabel* m_summary;
    QWidget* m_manual;
    QSlider* m_memory;
    QLabel* m_memoryLabel;
    QCheckBox* m_aikar;
    QLineEdit* m_jvmArgs;
    QLineEdit* m_java;
    QSpinBox* m_view;
    QSpinBox* m_simulation;
    QLabel* m_simulationLabel;
    QPushButton* m_save;
    QLabel* m_saved;
    bool m_loading = false;
};
