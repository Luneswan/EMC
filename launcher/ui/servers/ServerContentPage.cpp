// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerContentPage.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTabWidget>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "Application.h"
#include "Markdown.h"
#include "server/ServerContent.h"
#include "server/ServerInstance.h"
#include "server/ServerSoftware.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/widgets/ProjectDescriptionPage.h"
#include "ui/widgets/ProjectItem.h"

namespace {
// Above ProjectItem's TITLE, DESCRIPTION and INSTALLED roles (257-259).
constexpr int kProjectRole = Qt::UserRole + 11;
constexpr int kFileRole = Qt::UserRole + 12;
constexpr int kIconUrlRole = Qt::UserRole + 13;
constexpr int kVersionsRole = Qt::UserRole + 14;  // QVariantList of {id, label}
constexpr int kBodyRole = Qt::UserRole + 15;
constexpr int kAuthorRole = Qt::UserRole + 16;
constexpr int kDownloadsRole = Qt::UserRole + 17;
constexpr int kSlugRole = Qt::UserRole + 18;
constexpr int kPageSize = 30;

QString compactCount(qint64 n)
{
    if (n >= 1000000) {
        return QObject::tr("%1M").arg(n / 1000000.0, 0, 'f', 1);
    }
    if (n >= 1000) {
        return QObject::tr("%1K").arg(n / 1000);
    }
    return QString::number(n);
}

QNetworkReply* modrinthGet(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent());
    return APPLICATION->network()->get(request);
}

QString jsonList(const QStringList& values)
{
    QStringList quoted;
    for (const auto& v : values) {
        quoted << '"' + v + '"';
    }
    return '[' + quoted.join(',') + ']';
}

// Project icons, shared by every page so scrolling back and forth never refetches them.
QHash<QString, QIcon>& iconCache()
{
    static QHash<QString, QIcon> cache;
    return cache;
}

QListWidget* makeProjectList(QWidget* parent)
{
    auto* list = new QListWidget(parent);
    list->setItemDelegate(new ProjectItemDelegate(list));
    list->setIconSize(QSize(48, 48));
    list->setUniformItemSizes(true);
    list->setAlternatingRowColors(true);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return list;
}

QListWidgetItem* makeMessage(QListWidget* list, const QString& text)
{
    auto* item = new QListWidgetItem(list);
    item->setData(UserDataTypes::TITLE, text);
    item->setFlags(Qt::NoItemFlags);
    item->setSizeHint(QSize(0, 64));
    return item;
}
}  // namespace

ServerContentPage::ServerContentPage(ServerInstance* server, bool mods, QWidget* parent)
    : QWidget(parent), m_server(server), m_mods(mods)
{
    setAcceptDrops(true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 4, 6, 4);
    root->setSpacing(8);

    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs, 1);

    // Installed
    auto* installed = new QWidget(m_tabs);
    auto* installedLayout = new QVBoxLayout(installed);
    installedLayout->setContentsMargins(0, 10, 0, 0);
    m_installed = makeProjectList(installed);
    m_installed->setSelectionMode(QAbstractItemView::ExtendedSelection);
    installedLayout->addWidget(m_installed, 1);
    auto* installedButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add Files…"), installed);
    auto* folderButton = new QPushButton(tr("Open Folder"), installed);
    m_remove = new QPushButton(tr("Remove"), installed);
    m_remove->setEnabled(false);
    installedButtons->addWidget(addButton);
    installedButtons->addWidget(folderButton);
    installedButtons->addStretch(1);
    installedButtons->addWidget(m_remove);
    installedLayout->addLayout(installedButtons);
    m_tabs->addTab(installed, tr("Installed"));

    // Browse: the same layout as an instance's mod browser.
    auto* browse = new QWidget(m_tabs);
    auto* browseLayout = new QVBoxLayout(browse);
    browseLayout->setContentsMargins(0, 10, 0, 0);
    auto* searchRow = new QHBoxLayout;
    m_query = new QLineEdit(browse);
    m_query->setPlaceholderText(mods ? tr("Search for mods…") : tr("Search for plugins…"));
    m_query->setClearButtonEnabled(true);
    m_sort = new QComboBox(browse);
    m_sort->addItem(tr("Sort by Relevance"), "relevance");
    m_sort->addItem(tr("Sort by Downloads"), "downloads");
    m_sort->addItem(tr("Sort by Follows"), "follows");
    m_sort->addItem(tr("Sort by Newest"), "newest");
    m_sort->addItem(tr("Sort by Last Updated"), "updated");
    searchRow->addWidget(m_query, 1);
    searchRow->addWidget(m_sort);
    browseLayout->addLayout(searchRow);

    auto* splitter = new QSplitter(Qt::Horizontal, browse);
    splitter->setChildrenCollapsible(false);
    m_results = makeProjectList(splitter);
    m_description = new ProjectDescriptionPage(splitter);
    m_description->setOpenExternalLinks(true);
    m_description->setOpenLinks(true);
    splitter->addWidget(m_results);
    splitter->addWidget(m_description);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    browseLayout->addWidget(splitter, 1);

    auto* browseButtons = new QHBoxLayout;
    m_versions = new QComboBox(browse);
    m_versions->setMinimumWidth(220);
    m_select = new QPushButton(browse);
    m_install = new QPushButton(browse);
    m_install->setProperty("accent", true);
    browseButtons->addWidget(m_versions, 1);
    browseButtons->addWidget(m_select);
    browseButtons->addStretch(1);
    browseButtons->addWidget(m_install);
    browseLayout->addLayout(browseButtons);
    m_tabs->addTab(browse, tr("Browse"));

    m_note = new QLabel(this);
    m_note->setProperty("role", "note");
    m_note->setWordWrap(true);
    root->addWidget(m_note);

    m_searchDelay.setSingleShot(true);
    m_searchDelay.setInterval(350);
    connect(&m_searchDelay, &QTimer::timeout, this, [this] { search(); });
    connect(m_query, &QLineEdit::textChanged, this, [this] { m_searchDelay.start(); });
    connect(m_query, &QLineEdit::returnPressed, this, [this] { search(); });
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this] { search(); });
    connect(m_results, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) { showProject(item); });
    connect(m_results, &QListWidget::itemDoubleClicked, this, &ServerContentPage::toggleChosen);
    connect(static_cast<ProjectItemDelegate*>(m_results->itemDelegate()), &ProjectItemDelegate::checkboxClicked, this,
            [this](const QModelIndex& index) { toggleChosen(m_results->item(index.row())); });
    connect(m_results->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        // Load the next page when the list is scrolled to the end, like the instance browser.
        if (value == m_results->verticalScrollBar()->maximum() && m_offset < m_total && !m_searchReply) {
            search(true);
        }
    });
    connect(m_versions, &QComboBox::currentIndexChanged, this, [this] {
        // Picking another version of a project already selected keeps it selected, with that version.
        auto* item = m_results->currentItem();
        if (item && m_chosen.contains(item->data(kProjectRole).toString()) && m_versions->currentData().isValid()) {
            m_chosen[item->data(kProjectRole).toString()] = m_versions->currentData().toString();
        }
    });
    connect(m_select, &QPushButton::clicked, this, [this] { toggleChosen(m_results->currentItem()); });
    connect(m_install, &QPushButton::clicked, this, &ServerContentPage::installChosen);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1 && m_results->count() == 0) {
            search();  // the most downloaded, before anything is typed
        }
    });

    connect(m_installed, &QListWidget::itemSelectionChanged, this, [this] { m_remove->setEnabled(!m_installed->selectedItems().isEmpty()); });
    connect(static_cast<ProjectItemDelegate*>(m_installed->itemDelegate()), &ProjectItemDelegate::checkboxClicked, this,
            [this](const QModelIndex& index) {
                auto* item = m_installed->item(index.row());
                item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            });
    connect(m_installed, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        // Ticked means enabled: a disabled jar keeps its name with ".disabled" added.
        const QString path = item->data(kFileRole).toString();
        if (path.isEmpty()) {
            return;
        }
        const bool enable = item->checkState() == Qt::Checked;
        const bool isDisabled = path.endsWith(".disabled");
        if (enable == !isDisabled) {
            return;
        }
        const QString target = enable ? path.chopped(9) : path + ".disabled";
        if (QFile::rename(path, target)) {
            item->setData(kFileRole, target);
            setNote(m_server && m_server->isActive() ? tr("Restart the server to apply the change.") : QString());
        } else {
            setNote(tr("Could not rename %1. Is the server running?").arg(QFileInfo(path).fileName()));
            QSignalBlocker block(m_installed);
            item->setCheckState(enable ? Qt::Unchecked : Qt::Checked);
        }
    });
    connect(m_remove, &QPushButton::clicked, this, &ServerContentPage::removeSelected);
    connect(folderButton, &QPushButton::clicked, this, [this] {
        QDir().mkpath(folder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder()));
    });
    connect(addButton, &QPushButton::clicked, this, [this] {
        addFiles(QFileDialog::getOpenFileNames(this, tr("Add jar files"), QString(), tr("Jar files (*.jar)")));
    });

    updateSelectButton();
    updateInstallButton();
    refreshInstalled();
}

bool ServerContentPage::shouldDisplay() const
{
    const auto* info = m_server ? m_server->softwareInfo() : nullptr;
    return info && (m_mods ? info->mods : info->plugins);
}

QString ServerContentPage::folder() const
{
    return m_server ? ServerContent::contentDir(m_server, m_mods) : QString();
}

QStringList ServerContentPage::loaders() const
{
    const auto* info = m_server ? m_server->softwareInfo() : nullptr;
    QStringList result;
    if (!info) {
        return result;
    }
    for (const auto& loader : info->modrinthLoaders) {
        if (ServerContent::isModLoaderSet({ loader }) == m_mods) {
            result << loader;
        }
    }
    return result;
}

// ---------------------------------------------------------------- installed

namespace {
// Replies arrive after the lists may have been rebuilt, so rows are looked up again by their data
// instead of holding on to item pointers that may have been deleted.
QListWidgetItem* findItem(QListWidget* list, int role, const QString& value)
{
    if (!list || value.isEmpty()) {
        return nullptr;
    }
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(role).toString() == value) {
            return list->item(i);
        }
    }
    return nullptr;
}
}  // namespace

void ServerContentPage::refreshInstalled()
{
    QSignalBlocker block(m_installed);
    m_installed->clear();
    const QDir dir(folder());
    const auto files = dir.entryInfoList({ "*.jar", "*.jar.disabled" }, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const auto& file : files) {
        const bool disabled = file.fileName().endsWith(".disabled");
        QString name = file.fileName();
        name.chop(disabled ? 13 : 4);
        auto* item = new QListWidgetItem(m_installed);
        item->setData(UserDataTypes::TITLE, name);
        item->setData(UserDataTypes::DESCRIPTION, file.fileName());
        item->setIcon(QIcon::fromTheme(m_mods ? "loadermods" : "centralmods"));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(disabled ? Qt::Unchecked : Qt::Checked);
        item->setData(kFileRole, file.absoluteFilePath());
        item->setSizeHint(QSize(0, 56));
        item->setToolTip(tr("Tick to enable, untick to disable."));
    }
    if (files.isEmpty()) {
        makeMessage(m_installed, m_mods ? tr("No mods yet. Find some in Browse, or drop .jar files here.")
                                        : tr("No plugins yet. Find some in Browse, or drop .jar files here."));
    }
    m_tabs->setTabText(0, files.isEmpty() ? tr("Installed") : tr("Installed (%1)").arg(files.size()));
    identifyInstalled();
}

void ServerContentPage::identifyInstalled()
{
    // Ask Modrinth which projects the installed jars are (by SHA-1), for proper names, icons and
    // to mark them as installed in Browse. Hashing a big modpack takes a while, so it runs off the UI thread.
    QStringList paths;
    for (int i = 0; i < m_installed->count(); ++i) {
        const QString path = m_installed->item(i)->data(kFileRole).toString();
        if (!path.isEmpty()) {
            paths << path;
        }
    }
    const int generation = ++m_installedGeneration;
    auto* watcher = new QFutureWatcher<QHash<QString, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation == m_installedGeneration) {
            lookUpInstalled(watcher->result());
        }
    });
    watcher->setFuture(QtConcurrent::run([paths] {
        QHash<QString, QString> byHash;  // sha1 -> path
        for (const auto& path : paths) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            QCryptographicHash hash(QCryptographicHash::Sha1);
            hash.addData(&file);
            byHash.insert(QString::fromLatin1(hash.result().toHex()), path);
        }
        return byHash;
    }));
}

void ServerContentPage::lookUpInstalled(const QHash<QString, QString>& byHash)
{
    if (byHash.isEmpty()) {
        m_installedProjects.clear();
        markInstalled();
        return;
    }
    QJsonObject body{ { "hashes", QJsonArray::fromStringList(byHash.keys()) }, { "algorithm", "sha1" } };
    QNetworkRequest request(QUrl("https://api.modrinth.com/v2/version_files"));
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = APPLICATION->network()->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, byHash] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }
        const auto versions = QJsonDocument::fromJson(reply->readAll()).object();
        QHash<QString, QStringList> filesByProject;  // project id -> hashes
        QHash<QString, QString> versionNumbers;      // hash -> version number
        for (auto it = versions.begin(); it != versions.end(); ++it) {
            const auto version = it.value().toObject();
            filesByProject[version.value("project_id").toString()] << it.key();
            versionNumbers.insert(it.key(), version.value("version_number").toString());
        }
        m_installedProjects = QSet<QString>(filesByProject.keyBegin(), filesByProject.keyEnd());
        markInstalled();
        if (filesByProject.isEmpty()) {
            return;
        }
        QUrl url("https://api.modrinth.com/v2/projects");
        QUrlQuery query;
        query.addQueryItem("ids", jsonList(filesByProject.keys()));
        url.setQuery(query);
        auto* projects = modrinthGet(url);
        connect(projects, &QNetworkReply::finished, this, [this, projects, byHash, filesByProject, versionNumbers] {
            projects->deleteLater();
            if (projects->error() != QNetworkReply::NoError) {
                return;
            }
            QSignalBlocker block(m_installed);
            for (const auto value : QJsonDocument::fromJson(projects->readAll()).array()) {
                const auto project = value.toObject();
                for (const auto& hash : filesByProject.value(project.value("id").toString())) {
                    const QString path = byHash.value(hash);
                    auto* item = findItem(m_installed, kFileRole, path);
                    if (!item) {
                        continue;  // the list was rebuilt while this was loading
                    }
                    item->setData(UserDataTypes::TITLE, project.value("title").toString());
                    item->setData(UserDataTypes::DESCRIPTION, QString("%1 · %2").arg(versionNumbers.value(hash), QFileInfo(path).fileName()));
                    loadIcon(item, project.value("icon_url").toString());
                }
            }
        });
    });
}

void ServerContentPage::addFiles(const QStringList& files)
{
    if (files.isEmpty()) {
        return;
    }
    QDir().mkpath(folder());
    int added = 0;
    for (const auto& file : files) {
        if (!file.endsWith(".jar", Qt::CaseInsensitive)) {
            continue;
        }
        const QString target = QDir(folder()).filePath(QFileInfo(file).fileName());
        if (QFileInfo(file).canonicalFilePath() == QFileInfo(target).canonicalFilePath()) {
            ++added;  // already here (dropped from this very folder)
            continue;
        }
        // Copy first, and replace the old file only once the copy is complete.
        const QString part = target + ".part";
        QFile::remove(part);
        if (QFile::copy(file, part)) {
            QFile::remove(target);
            added += QFile::rename(part, target) ? 1 : 0;
        }
        QFile::remove(part);
    }
    refreshInstalled();
    setNote(added == 0 ? tr("Only .jar files can be added.")
                       : (m_server && m_server->isActive() ? tr("Added %1. Restart the server to load it.").arg(added)
                                                            : tr("Added %1.").arg(added)));
}

void ServerContentPage::removeSelected()
{
    int removed = 0;
    for (auto* item : m_installed->selectedItems()) {
        removed += QFile::remove(item->data(kFileRole).toString()) ? 1 : 0;
    }
    refreshInstalled();
    setNote(removed == 0 ? tr("Could not remove the file. Stop the server first.")
                         : (m_server && m_server->isActive() ? tr("Removed. Restart the server to apply.") : tr("Removed.")));
}

// ---------------------------------------------------------------- browse

void ServerContentPage::search(bool more)
{
    if (!m_server) {
        return;
    }
    if (m_searchReply) {
        m_searchReply->abort();
    }
    if (!more) {
        m_offset = 0;
        m_total = 0;
    }
    QStringList loaderFacets;
    for (const auto& loader : loaders()) {
        loaderFacets << QString("\"categories:%1\"").arg(loader);
    }
    QStringList facets{ QString("[%1]").arg(loaderFacets.join(',')) };
    if (!m_server->mcVersion().isEmpty()) {
        facets << QString("[\"versions:%1\"]").arg(m_server->mcVersion());
    }
    if (m_mods) {
        facets << "[\"server_side:required\",\"server_side:optional\"]" << "[\"project_type:mod\"]";
    }
    const QString text = m_query->text().trimmed();
    QString index = m_sort->currentData().toString();
    if (index == "relevance" && text.isEmpty()) {
        index = "downloads";  // relevance means nothing without a query
    }
    QUrl url("https://api.modrinth.com/v2/search");
    QUrlQuery query;
    if (!text.isEmpty()) {
        query.addQueryItem("query", text);
    }
    query.addQueryItem("facets", QString("[%1]").arg(facets.join(',')));
    query.addQueryItem("index", index);
    query.addQueryItem("offset", QString::number(m_offset));
    query.addQueryItem("limit", QString::number(kPageSize));
    url.setQuery(query);

    if (!more) {
        m_results->clear();
        m_description->setHtml({});
        makeMessage(m_results, tr("Searching…"));
        updateSelectButton();
    }

    auto* reply = modrinthGet(url);
    m_searchReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, more] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            return;
        }
        if (!more) {
            m_results->clear();
        }
        if (reply->error() != QNetworkReply::NoError) {
            makeMessage(m_results, tr("Could not reach Modrinth: %1").arg(reply->errorString()));
            return;
        }
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        const auto hits = object.value("hits").toArray();
        m_total = object.value("total_hits").toInt();
        m_offset += hits.size();
        for (const auto value : hits) {
            const auto hit = value.toObject();
            const QString project = hit.value("project_id").toString();
            auto* item = new QListWidgetItem(m_results);
            item->setData(UserDataTypes::TITLE, hit.value("title").toString());
            item->setData(UserDataTypes::DESCRIPTION, hit.value("description").toString());
            item->setData(kProjectRole, project);
            item->setData(kSlugRole, hit.value("slug").toString());
            item->setData(kAuthorRole, hit.value("author").toString());
            item->setData(kDownloadsRole, hit.value("downloads").toDouble());
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(m_chosen.contains(project) ? Qt::Checked : Qt::Unchecked);
            item->setSizeHint(QSize(0, 64));
            item->setIcon(QIcon::fromTheme(m_mods ? "loadermods" : "centralmods"));
            loadIcon(item, hit.value("icon_url").toString());
        }
        markInstalled();
        if (m_results->count() == 0) {
            makeMessage(m_results, m_query->text().trimmed().isEmpty()
                                       ? tr("Nothing for Minecraft %1 on this server type yet.").arg(m_server->mcVersion())
                                       : tr("No %1 found for “%2”.").arg(m_mods ? tr("mods") : tr("plugins"), m_query->text().trimmed()));
        } else if (!more) {
            m_results->setCurrentRow(0);
        }
    });
}

void ServerContentPage::loadIcon(QListWidgetItem* item, const QString& url)
{
    if (url.isEmpty()) {
        return;
    }
    if (auto it = iconCache().constFind(url); it != iconCache().constEnd()) {
        item->setIcon(*it);
        return;
    }
    item->setData(kIconUrlRole, url);
    auto* reply = modrinthGet(QUrl(url));
    QPointer<QListWidget> list = item->listWidget();
    connect(reply, &QNetworkReply::finished, this, [reply, url, list] {
        reply->deleteLater();
        QPixmap pixmap;
        if (reply->error() != QNetworkReply::NoError || !pixmap.loadFromData(reply->readAll())) {
            return;
        }
        const QIcon icon(pixmap.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        iconCache().insert(url, icon);
        if (!list) {
            return;
        }
        // Every row showing this project (the list may have been rebuilt since).
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(kIconUrlRole).toString() == url) {
                list->item(i)->setIcon(icon);
            }
        }
    });
}

void ServerContentPage::markInstalled()
{
    for (int i = 0; i < m_results->count(); ++i) {
        auto* item = m_results->item(i);
        item->setData(UserDataTypes::INSTALLED, m_installedProjects.contains(item->data(kProjectRole).toString()));
    }
    updateSelectButton();
}

void ServerContentPage::showProject(QListWidgetItem* item)
{
    m_description->flush();
    if (!item || item->data(kProjectRole).toString().isEmpty()) {
        m_description->setHtml({});
        updateSelectButton();
        return;
    }
    const QString project = item->data(kProjectRole).toString();
    const QString page = QString("https://modrinth.com/%1/%2").arg(m_mods ? "mod" : "plugin", item->data(kSlugRole).toString());
    auto render = [this, page](QListWidgetItem* item) {
        QString html = QString("<h2><a href=\"%1\">%2</a></h2>").arg(page, item->data(UserDataTypes::TITLE).toString().toHtmlEscaped());
        html += tr("by %1 · %2 downloads")
                    .arg(item->data(kAuthorRole).toString().toHtmlEscaped(), compactCount(qint64(item->data(kDownloadsRole).toDouble())));
        html += "<hr>";
        const QString body = item->data(kBodyRole).toString();
        html += body.isEmpty() ? item->data(UserDataTypes::DESCRIPTION).toString().toHtmlEscaped() : markdownToHTML(body);
        m_description->setHtml(html);
    };
    render(item);
    if (item->data(kBodyRole).toString().isEmpty()) {
        auto* reply = modrinthGet(QUrl("https://api.modrinth.com/v2/project/" + project));
        connect(reply, &QNetworkReply::finished, this, [this, reply, project, render] {
            reply->deleteLater();
            auto* item = findItem(m_results, kProjectRole, project);
            if (reply->error() != QNetworkReply::NoError || !item) {
                return;
            }
            item->setData(kBodyRole, QJsonDocument::fromJson(reply->readAll()).object().value("body").toString());
            if (m_results->currentItem() == item) {
                render(item);
            }
        });
    }
    loadVersions(item);
}

void ServerContentPage::loadVersions(QListWidgetItem* item)
{
    if (m_versionReply) {
        m_versionReply->abort();
    }
    QSignalBlocker block(m_versions);
    m_versions->clear();
    const QVariantList cached = item->data(kVersionsRole).toList();
    const QString project = item->data(kProjectRole).toString();
    auto fill = [this, project](const QVariantList& versions) {
        QSignalBlocker block(m_versions);
        m_versions->clear();
        for (const auto& v : versions) {
            const auto map = v.toMap();
            m_versions->addItem(map.value("label").toString(), map.value("id"));
        }
        // Show the version that is selected for install, if any.
        if (const int index = m_versions->findData(m_chosen.value(project)); index >= 0) {
            m_versions->setCurrentIndex(index);
        }
        updateSelectButton();
    };
    if (!cached.isEmpty()) {
        fill(cached);
        return;
    }
    m_versions->addItem(tr("Loading versions…"));
    updateSelectButton();

    QUrl url(QString("https://api.modrinth.com/v2/project/%1/version").arg(project));
    QUrlQuery query;
    query.addQueryItem("loaders", jsonList(loaders()));
    if (!m_server->mcVersion().isEmpty()) {
        query.addQueryItem("game_versions", jsonList({ m_server->mcVersion() }));
    }
    url.setQuery(query);
    auto* reply = modrinthGet(url);
    m_versionReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, project, fill] {
        reply->deleteLater();
        auto* item = findItem(m_results, kProjectRole, project);
        if (reply->error() == QNetworkReply::OperationCanceledError || !item) {
            return;
        }
        QVariantList versions;
        for (const auto value : QJsonDocument::fromJson(reply->readAll()).array()) {
            const auto version = value.toObject();
            QString label = version.value("version_number").toString();
            const QString type = version.value("version_type").toString();
            if (type != "release") {
                label += QString(" (%1)").arg(type);
            }
            versions << QVariantMap{ { "id", version.value("id").toString() }, { "label", label } };
        }
        item->setData(kVersionsRole, versions);
        if (m_results->currentItem() == item) {
            if (versions.isEmpty()) {
                QSignalBlocker block(m_versions);
                m_versions->clear();
                m_versions->addItem(tr("No version for Minecraft %1").arg(m_server->mcVersion()));
                updateSelectButton();
            } else {
                fill(versions);
            }
        }
    });
}

void ServerContentPage::toggleChosen(QListWidgetItem* item)
{
    if (!item || item->data(kProjectRole).toString().isEmpty()) {
        return;
    }
    const QString project = item->data(kProjectRole).toString();
    if (m_chosen.contains(project)) {
        m_chosen.remove(project);
        m_chosenTitles.remove(project);
    } else {
        // Before the versions have loaded the newest compatible one is used.
        const QVariant version = m_results->currentItem() == item ? m_versions->currentData() : QVariant();
        m_chosen.insert(project, version.isValid() ? version.toString() : QString());
        m_chosenTitles.insert(project, item->data(UserDataTypes::TITLE).toString());
    }
    item->setCheckState(m_chosen.contains(project) ? Qt::Checked : Qt::Unchecked);
    updateSelectButton();
    updateInstallButton();
}

void ServerContentPage::updateSelectButton()
{
    auto* item = m_results->currentItem();
    const bool valid = item && !item->data(kProjectRole).toString().isEmpty();
    const bool chosen = valid && m_chosen.contains(item->data(kProjectRole).toString());
    const bool hasVersion = m_versions->currentData().isValid();
    m_versions->setEnabled(valid && hasVersion);
    if (chosen) {
        m_select->setText(tr("Deselect %1").arg(kind()));
        m_select->setEnabled(true);
    } else if (valid && item->data(UserDataTypes::INSTALLED).toBool()) {
        m_select->setText(tr("Already installed"));
        m_select->setEnabled(false);
    } else {
        m_select->setText(tr("Select %1 for install").arg(kind()));
        m_select->setEnabled(valid && hasVersion);
    }
}

void ServerContentPage::updateInstallButton()
{
    const int count = int(m_chosen.size());
    if (count == 0) {
        m_install->setText(tr("Install"));
    } else if (count == 1) {
        m_install->setText(m_mods ? tr("Install 1 mod") : tr("Install 1 plugin"));
    } else {
        m_install->setText(m_mods ? tr("Install %1 mods").arg(count) : tr("Install %1 plugins").arg(count));
    }
    m_install->setEnabled(count > 0);
    m_install->setToolTip(QStringList(m_chosenTitles.values()).join(", "));
}

void ServerContentPage::installChosen()
{
    if (!m_server || m_chosen.isEmpty()) {
        return;
    }
    QList<QPair<QString, QString>> projects;
    for (auto it = m_chosen.cbegin(); it != m_chosen.cend(); ++it) {
        projects.append({ it.key(), it.value() });
    }
    QDir().mkpath(folder());
    auto task = std::make_unique<ModrinthServerInstallTask>(projects, loaders(), m_server->mcVersion(), folder());
    ProgressDialog progress(this);
    progress.execWithTask(task.get());
    if (!task->wasSuccessful()) {
        setNote(tr("Could not install: %1").arg(task->failReason()));
        refreshInstalled();
        return;
    }
    // Clear the selection, then show what happened.
    QStringList installedTitles;
    QStringList skippedTitles;
    for (auto it = m_chosenTitles.cbegin(); it != m_chosenTitles.cend(); ++it) {
        (task->missing().contains(it.key()) ? skippedTitles : installedTitles) << it.value();
    }
    m_chosen.clear();
    m_chosenTitles.clear();
    for (int i = 0; i < m_results->count(); ++i) {
        if (!m_results->item(i)->data(kProjectRole).toString().isEmpty()) {
            m_results->item(i)->setCheckState(Qt::Unchecked);
        }
    }
    updateInstallButton();
    refreshInstalled();

    QString message = tr("Installed %1.").arg(installedTitles.join(", "));
    const int extras = int(task->installedFiles().size()) - int(task->titles().size());
    if (extras > 0) {
        message += ' ' + (extras == 1 ? tr("Also added 1 file it needs.") : tr("Also added %1 files they need.").arg(extras));
    }
    if (!task->missing().isEmpty()) {
        message += ' ' + tr("Skipped %1: no version for this server.").arg(skippedTitles.join(", "));
    }
    if (m_server->isActive() && !task->installedFiles().isEmpty()) {
        message += ' ' + tr("Restart the server to load them.");
    }
    setNote(message);
}

void ServerContentPage::setNote(const QString& text)
{
    m_note->setText(text);
}

void ServerContentPage::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ServerContentPage::dropEvent(QDropEvent* event)
{
    QStringList files;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            files << url.toLocalFile();
        }
    }
    addFiles(files);
}
