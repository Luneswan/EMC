// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QObject>
#include <QPointer>

class QMenu;
class QSystemTrayIcon;
class ServerList;

// Keeps servers reachable when the main window is closed: a tray icon with each server's
// Start/Stop, plus starting EMC when the user signs in.
class ServerTray : public QObject {
    Q_OBJECT
   public:
    explicit ServerTray(ServerList* servers, QObject* parent = nullptr);
    ~ServerTray() override;

    // True once EMC lives in the tray: it then keeps running with no windows open.
    bool keepsAppAlive() const { return m_inBackground; }
    // The main window closed: stay in the tray if a server is running (or EMC started in the tray).
    void mainWindowClosed();
    void enterBackground(bool notify);
    // A system notification, shown even while the tray icon is hidden.
    void notify(const QString& title, const QString& message);

    // "Start EMC with Windows", or "when you log in" elsewhere.
    static QString startAtLoginText();
    static bool startWithWindows();
    static void setStartWithWindows(bool on);

   signals:
    void openRequested();
    void quitRequested();

   private:
    void rebuildMenu();
    void updateVisibility();

    ServerList* m_servers;
    QSystemTrayIcon* m_icon;
    QMenu* m_menu;
    bool m_inBackground = false;
};
