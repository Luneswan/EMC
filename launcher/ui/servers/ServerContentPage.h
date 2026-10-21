// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "ui/pages/BasePage.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QNetworkReply;
class QPushButton;
class QTabWidget;
class ProjectDescriptionPage;
class ServerInstance;

// Plugins or mods for one server, laid out like an instance's mod browser: what is installed
// (enable, disable, remove, add files) and a Modrinth browser where projects are selected (with a
// version) and then installed together, dependencies included.
class ServerContentPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    ServerContentPage(ServerInstance* server, bool mods, QWidget* parent = nullptr);
    QString id() const override { return m_mods ? "mods" : "plugins"; }
    QString displayName() const override { return m_mods ? tr("Mods") : tr("Plugins"); }
    QIcon icon() const override { return QIcon::fromTheme(m_mods ? "loadermods" : "centralmods"); }
    bool shouldDisplay() const override;
    void openedImpl() override { refreshInstalled(); }

   protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

   private:
    QString folder() const;
    QStringList loaders() const;
    QString kind() const { return m_mods ? tr("mod") : tr("plugin"); }

    void refreshInstalled();
    void identifyInstalled();
    void lookUpInstalled(const QHash<QString, QString>& byHash);
    void addFiles(const QStringList& files);
    void removeSelected();

    void search(bool more = false);
    void showProject(QListWidgetItem* item);
    void loadVersions(QListWidgetItem* item);
    void toggleChosen(QListWidgetItem* item);
    void updateSelectButton();
    void updateInstallButton();
    void installChosen();
    void loadIcon(QListWidgetItem* item, const QString& url);
    void markInstalled();
    void setNote(const QString& text);

    QPointer<ServerInstance> m_server;
    bool m_mods;
    QTabWidget* m_tabs;
    QListWidget* m_installed;
    QPushButton* m_remove;
    QLineEdit* m_query;
    QComboBox* m_sort;
    QListWidget* m_results;
    ProjectDescriptionPage* m_description;
    QComboBox* m_versions;
    QPushButton* m_select;
    QPushButton* m_install;
    QLabel* m_note;
    QTimer m_searchDelay;
    QPointer<QNetworkReply> m_searchReply;
    QPointer<QNetworkReply> m_versionReply;

    QHash<QString, QString> m_chosen;  // project id -> version id, selected for install
    QHash<QString, QString> m_chosenTitles;
    QSet<QString> m_installedProjects;  // Modrinth project ids of the installed files
    int m_offset = 0;
    int m_installedGeneration = 0;
    int m_total = 0;
};
