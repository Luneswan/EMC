// SPDX-License-Identifier: GPL-3.0-only
#include "EmcUpdater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSysInfo>

#include "BuildConfig.h"
#include "Version.h"

namespace {

const char* kLatestRelease = "https://api.github.com/repos/Luneswan/EMC/releases/latest";
constexpr int kFirstCheckMs = 15 * 1000;
constexpr int kRecheckMs = 6 * 60 * 60 * 1000;

QString shellQuote(QString s)
{
    return "'" + s.replace("'", "'\\''") + "'";
}

}  // namespace

EmcUpdater::EmcUpdater(QNetworkAccessManager* network, const QString& workDir, QObject* parent)
    : QObject(parent), m_network(network), m_workDir(workDir), m_kind(Kind::Manual)
{
    const QString appDir = QCoreApplication::applicationDirPath();
#if defined(Q_OS_WIN)
    // Only the installed copy has an uninstaller next to it; a portable zip is left to the player.
    if (QFile::exists(QDir(appDir).filePath("uninstall.exe"))) {
        m_kind = Kind::Installer;
        m_assetName = "EMC-Setup.exe";
    }
#elif defined(Q_OS_MACOS)
    if (appDir.endsWith(".app/Contents/MacOS")) {
        m_kind = Kind::MacApp;
        m_assetName = "EMC-macOS.dmg";
    }
#elif defined(Q_OS_LINUX)
    if (!qEnvironmentVariable("APPIMAGE").isEmpty()) {
        m_kind = Kind::AppImage;
        const bool arm = QSysInfo::currentCpuArchitecture().startsWith("arm");
        m_assetName = arm ? "EMC-Linux-aarch64.AppImage" : "EMC-Linux-x86_64.AppImage";
    }
#endif
    // Updates this EMC already is (or is newer than) have done their job; each one is ~45 MB.
    const Version running(BuildConfig.versionString());
    for (const QFileInfo& file : QDir(m_workDir).entryInfoList(QDir::Files)) {
        if (!(Version(file.fileName().section('-', 0, 0)) > running))
            QFile::remove(file.absoluteFilePath());
    }
    m_timer.setInterval(kRecheckMs);
    connect(&m_timer, &QTimer::timeout, this, &EmcUpdater::checkNow);
    // A downloaded update that was never applied by a restart still goes in when EMC closes.
    connect(qApp, &QCoreApplication::aboutToQuit, this, &EmcUpdater::installOnQuit);
}

void EmcUpdater::start()
{
    QTimer::singleShot(kFirstCheckMs, this, &EmcUpdater::checkNow);
    m_timer.start();
}

void EmcUpdater::checkNow()
{
    if (isReady() || m_download)
        return;
    QNetworkRequest request{ QUrl(kLatestRelease) };
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", QString("EMC/%1").arg(BuildConfig.versionString()).toUtf8());
    auto* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onReleaseReply(reply); });
}

void EmcUpdater::onReleaseReply(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        qInfo() << "Update check failed:" << reply->errorString();
        return;
    }
    const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
    QString tag = release.value("tag_name").toString();
    if (tag.startsWith('v'))
        tag.remove(0, 1);
    if (tag.isEmpty() || release.value("draft").toBool() || release.value("prerelease").toBool())
        return;
    if (!(Version(tag) > Version(BuildConfig.versionString()))) {
        qInfo() << "EMC is up to date:" << BuildConfig.versionString();
        return;
    }
    qInfo() << "EMC update found:" << tag;
    m_latestVersion = tag;
    m_releasePage = release.value("html_url").toString();

    if (m_kind != Kind::Manual) {
        for (const auto& value : release.value("assets").toArray()) {
            const QJsonObject asset = value.toObject();
            if (asset.value("name").toString() != m_assetName)
                continue;
            QString digest = asset.value("digest").toString();
            digest = digest.startsWith("sha256:") ? digest.mid(7) : QString();
            download(asset.value("browser_download_url").toString(), m_assetName,
                     asset.value("size").toInteger(-1), digest);
            return;
        }
        qInfo() << "The release has no" << m_assetName << "- showing the download page instead";
    }
    emit updateAvailable(tag);
}

void EmcUpdater::download(const QString& url, const QString& name, qint64 size, const QString& sha256)
{
    QDir().mkpath(m_workDir);
    const QString target = QDir(m_workDir).filePath(m_latestVersion + "-" + name);
    // Already fetched by an earlier EMC session.
    if (QFileInfo(target).size() == size && size > 0) {
        m_readyFile = target;
        emit updateReady(m_latestVersion);
        return;
    }
    auto* file = new QFile(target + ".part", this);
    if (!file->open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save the update to" << file->fileName();
        file->deleteLater();
        emit updateAvailable(m_latestVersion);
        return;
    }
    QNetworkRequest request{ QUrl(url) };
    request.setRawHeader("User-Agent", QString("EMC/%1").arg(BuildConfig.versionString()).toUtf8());
    m_download = m_network->get(request);
    auto* hash = new QCryptographicHash(QCryptographicHash::Sha256);
    connect(m_download, &QNetworkReply::readyRead, this, [this, file, hash] {
        const QByteArray chunk = m_download->readAll();
        hash->addData(chunk);
        file->write(chunk);
    });
    connect(m_download, &QNetworkReply::finished, this, [this, file, hash, target, size, sha256] {
        auto* reply = m_download.data();
        reply->deleteLater();
        const QByteArray chunk = reply->readAll();
        hash->addData(chunk);
        file->write(chunk);
        file->close();
        const QString got = QString::fromLatin1(hash->result().toHex());
        delete hash;
        const bool ok = reply->error() == QNetworkReply::NoError && (size <= 0 || file->size() == size) &&
                        (sha256.isEmpty() || got.compare(sha256, Qt::CaseInsensitive) == 0);
        if (!ok) {
            qWarning() << "Update download failed:" << reply->errorString() << "size" << file->size() << "expected" << size;
            file->remove();
            file->deleteLater();
            emit updateAvailable(m_latestVersion);
            return;
        }
        QFile::remove(target);
        file->rename(target);
        file->deleteLater();
#ifndef Q_OS_WIN
        QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
#endif
        qInfo() << "EMC" << m_latestVersion << "is downloaded and ready:" << target;
        m_readyFile = target;
        emit updateReady(m_latestVersion);
    });
}

void EmcUpdater::restartToUpdate()
{
    if (!isReady())
        return;
    m_relaunch = true;
    QCoreApplication::quit();
}

void EmcUpdater::installOnQuit()
{
    // Connected after the server list's own aboutToQuit handler, so servers have saved and stopped by now.
    if (isReady() && !m_launched)
        launchInstall(m_relaunch);
}

bool EmcUpdater::launchInstall(bool relaunch)
{
    if (!isReady() || m_launched)
        return false;
    bool started = false;
    switch (m_kind) {
        case Kind::Installer: {
            // The installer closes this EMC, installs over it and, with /Relaunch, opens the new one.
            QStringList args{ "/S" };
            if (relaunch)
                args << "/Relaunch";
            started = QProcess::startDetached(m_readyFile, args);
            break;
        }
        case Kind::AppImage: {
            // A running AppImage can be renamed over on Linux; the new file takes its place for the next start.
            const QString appImage = qEnvironmentVariable("APPIMAGE");
            // Start the new one only once this EMC has exited, or it would just hand over to the closing copy.
            const QString script = QString("while kill -0 %1 2>/dev/null; do sleep 0.5; done; mv -f %2 %3 && chmod +x %3 %4")
                                       .arg(QString::number(QCoreApplication::applicationPid()), shellQuote(m_readyFile),
                                            shellQuote(appImage), relaunch ? "&& exec " + shellQuote(appImage) : QString());
            started = QProcess::startDetached("/bin/sh", { "-c", script });
            break;
        }
        case Kind::MacApp: {
            // Wait for this EMC to exit, then copy the new EMC.app over the old one and open it.
            const QString bundle = QDir(QCoreApplication::applicationDirPath() + "/../..").canonicalPath();
            const QString script =
                QString("while kill -0 %1 2>/dev/null; do sleep 0.5; done; "
                        "m=$(mktemp -d) && hdiutil attach -nobrowse -readonly -mountpoint \"$m\" %2 >/dev/null && "
                        "rm -rf %3.old && mv %3 %3.old && ditto \"$m/EMC.app\" %3 && rm -rf %3.old; "
                        "hdiutil detach \"$m\" >/dev/null 2>&1; [ -d %3 ] || mv %3.old %3; %4")
                    .arg(QString::number(QCoreApplication::applicationPid()), shellQuote(m_readyFile), shellQuote(bundle),
                         relaunch ? "open " + shellQuote(bundle) : QString());
            started = QProcess::startDetached("/bin/sh", { "-c", script });
            break;
        }
        case Kind::Manual:
            break;
    }
    m_launched = started;
    if (!started)
        qWarning() << "Could not start the EMC update from" << m_readyFile;
    return started;
}
