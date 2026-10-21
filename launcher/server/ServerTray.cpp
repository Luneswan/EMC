// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerTray.h"

#include <QStandardPaths>
#include <QFileInfo>
#include <QFile>
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
const char* kRunValue = "EMC";
#endif

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
    auto* autostart = m_menu->addAction(startAtLoginText());
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

QString ServerTray::startAtLoginText()
{
#ifdef Q_OS_WIN
    return tr("Start EMC with Windows");
#else
    return tr("Start EMC when you log in");
#endif
}

namespace {
// The program to start: the AppImage itself rather than its temporary mount, when EMC runs from one.
QString startProgram()
{
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    return appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
}

#ifdef Q_OS_MACOS
QString launchAgentPath()
{
    return QDir::homePath() + "/Library/LaunchAgents/io.github.luneswan.EMC.plist";
}
#elif !defined(Q_OS_WIN)
QString autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/emc.desktop";
}
#endif
}  // namespace

bool ServerTray::startWithWindows()
{
#if defined(Q_OS_WIN)
    QSettings run(kRunKey, QSettings::NativeFormat);
    return run.contains(kRunValue);
#elif defined(Q_OS_MACOS)
    return QFileInfo::exists(launchAgentPath());
#else
    return QFileInfo::exists(autostartPath());
#endif
}

void ServerTray::setStartWithWindows(bool on)
{
    // Starts quietly in the tray, with this data folder, so autostart servers come up.
    const QString data = QDir::toNativeSeparators(APPLICATION->dataRoot());
#if defined(Q_OS_WIN)
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (on) {
        const QString exe = QDir::toNativeSeparators(startProgram());
        run.setValue(kRunValue, QString("\"%1\" --tray --dir \"%2\"").arg(exe, data));
    } else {
        run.remove(kRunValue);
    }
#elif defined(Q_OS_MACOS)
    const QString path = launchAgentPath();
    if (!on) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        auto xml = [](const QString& text) { return text.toHtmlEscaped(); };
        file.write(QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                           "<plist version=\"1.0\"><dict>\n"
                           "  <key>Label</key><string>io.github.luneswan.EMC</string>\n"
                           "  <key>ProgramArguments</key><array>\n"
                           "    <string>%1</string><string>--tray</string><string>--dir</string><string>%2</string>\n"
                           "  </array>\n"
                           "  <key>RunAtLoad</key><true/>\n"
                           "</dict></plist>\n")
                       .arg(xml(startProgram()), xml(data))
                       .toUtf8());
    }
#else
    const QString path = autostartPath();
    if (!on) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        auto quote = [](QString text) { return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\""; };
        file.write(QString("[Desktop Entry]\nType=Application\nName=EMC\nComment=Start EMC in the tray\n"
                           "Exec=%1 --tray --dir %2\nIcon=io.github.luneswan.EMC\nTerminal=false\nX-GNOME-Autostart-enabled=true\n")
                       .arg(quote(startProgram()), quote(data))
                       .toUtf8());
    }
#endif
}
