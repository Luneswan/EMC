// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QDialog>
#include <QFrame>
#include <QPointer>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
class RconClient;
class RemoteServer;

// Adds or edits a hosted server: its address, and optionally RCON and the host's control panel.
class RemoteServerDialog : public QDialog {
    Q_OBJECT
   public:
    explicit RemoteServerDialog(RemoteServer* server, QWidget* parent = nullptr);  // nullptr: add a new one
    RemoteServer* server() const { return m_server; }
    void accept() override;

   private:
    RemoteServer* m_server;
    QLineEdit* m_name;
    QLineEdit* m_address;
    QComboBox* m_edition;
    QSpinBox* m_rconPort;
    QLineEdit* m_rconPassword;
    QLineEdit* m_panel;
};

// One hosted server: whether it is up, who is on it, its console over RCON, and Play.
class RemoteServerWindow : public QDialog {
    Q_OBJECT
   public:
    static void open(RemoteServer* server);

   private:
    explicit RemoteServerWindow(RemoteServer* server);
    void refresh();
    void connectRcon();
    void appendConsole(const QString& text);

    QPointer<RemoteServer> m_server;
    QLabel* m_title;
    QLabel* m_state;
    QLabel* m_address;
    QLabel* m_motd;
    QLabel* m_players;
    QLabel* m_consoleNote;
    QPlainTextEdit* m_console;
    QLineEdit* m_command;
    QPushButton* m_panel;
    RconClient* m_rcon;
};

// A hosted server in the Servers list.
class RemoteServerCard : public QFrame {
    Q_OBJECT
   public:
    explicit RemoteServerCard(RemoteServer* server, QWidget* parent = nullptr);

   protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

   private:
    void refresh();

    QPointer<RemoteServer> m_server;
    QLabel* m_dot;
    QLabel* m_name;
    QLabel* m_detail;
    QLabel* m_status;
};

namespace RemoteServers {
// Asks to remove the server from EMC; the server itself is not touched.
void remove(RemoteServer* server, QWidget* parent);
}  // namespace RemoteServers
