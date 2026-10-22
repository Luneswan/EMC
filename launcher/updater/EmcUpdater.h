// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

// EMC keeps itself up to date from its GitHub releases: it looks for a newer release, downloads it in the
// background and installs it when EMC restarts or quits. Nothing replaces the running copy behind the player's back.
class EmcUpdater : public QObject {
    Q_OBJECT
   public:
    enum class Kind {
        Manual,    // portable zip or a source build: EMC can only point at the download page
        Installer, // Windows, installed with EMC-Setup.exe
        AppImage,  // Linux AppImage: the file is swapped for the new one
        MacApp,    // macOS: EMC.app is replaced from the new DMG
    };

    EmcUpdater(QNetworkAccessManager* network, const QString& workDir, QObject* parent = nullptr);

    void start();          // first check shortly after launch, then every few hours
    void checkNow();

    bool isReady() const { return !m_readyFile.isEmpty(); }
    bool isAvailable() const { return !m_latestVersion.isEmpty(); }
    QString latestVersion() const { return m_latestVersion; }
    QString releasePage() const { return m_releasePage; }
    Kind kind() const { return m_kind; }

    // Quits EMC; once its servers have stopped, the update installs and the new EMC opens.
    void restartToUpdate();

   signals:
    void updateAvailable(const QString& version);  // newer release found (and not installable in place)
    void updateReady(const QString& version);      // downloaded and checked, waiting for a restart

   private:
    void onReleaseReply(QNetworkReply* reply);
    void download(const QString& url, const QString& name, qint64 size, const QString& sha256);
    bool launchInstall(bool relaunch);
    void installOnQuit();

    QNetworkAccessManager* m_network;
    QString m_workDir;
    QTimer m_timer;
    Kind m_kind;
    QString m_assetName;
    QString m_latestVersion;
    QString m_releasePage;
    QString m_readyFile;
    QPointer<QNetworkReply> m_download;
    bool m_launched = false;
    bool m_relaunch = false;
};
