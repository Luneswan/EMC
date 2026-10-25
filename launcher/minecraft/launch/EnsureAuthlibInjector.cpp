// SPDX-License-Identifier: GPL-3.0-only
#include "EnsureAuthlibInjector.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "Application.h"
#include "FileSystem.h"
#include "net/ChecksumValidator.h"
#include "net/Request.h"

namespace {
const QUrl kLatest("https://authlib-injector.yushi.moe/artifact/latest.json");
const char* kJarName = "authlib-injector.jar";
const char* kInfoName = "authlib-injector.json";  // {"version", "sha256"} of the jar beside it

QString fileSha256(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    return QString::fromLatin1(hash.result().toHex());
}
}  // namespace

EnsureAuthlibInjector::EnsureAuthlibInjector(LaunchTask* parent, AuthSessionPtr session) : LaunchStep(parent), m_session(std::move(session))
{}

void EnsureAuthlibInjector::executeTask()
{
    if (!m_session || m_session->yggdrasil_server.isEmpty()) {
        emitSucceeded();
        return;
    }
    m_dir = FS::PathCombine(APPLICATION->dataRoot(), "authlib-injector");
    FS::ensureFolderPathExists(m_dir);

    auto [request, response] = Net::Request::makeByteArray(kLatest);
    m_job.reset(new NetJob("authlib-injector version", APPLICATION->network()));
    m_job->setAskRetry(false);
    m_job->addNetAction(request);
    connect(m_job.get(), &Task::finished, this, [this, request, response] {
        if (request->error() != QNetworkReply::NoError) {
            useCachedOrFail(request->errorString());
            return;
        }
        onLatestDone(response);
    });
    m_job->start();
}

void EnsureAuthlibInjector::onLatestDone(QByteArray* response)
{
    const auto latest = QJsonDocument::fromJson(*response).object();
    const QString version = latest.value("version").toString();
    const QString sha256 = latest.value("checksums").toObject().value("sha256").toString().toLower();
    const QUrl url(latest.value("download_url").toString());
    if (version.isEmpty() || sha256.size() != 64 || !url.isValid() || url.scheme() != "https") {
        useCachedOrFail(tr("the version list could not be read"));
        return;
    }

    const QString jar = FS::PathCombine(m_dir, kJarName);
    const auto info = QJsonDocument::fromJson(FS::read(FS::PathCombine(m_dir, kInfoName)).value_or(QByteArray())).object();
    if (info.value("sha256").toString() == sha256 && fileSha256(jar) == sha256) {
        succeedWith(jar);
        return;
    }

    emit logLine(tr("Downloading authlib-injector %1 for %2 skins...")
                     .arg(version, m_session->yggdrasil_optional ? QStringLiteral("Ely.by") : QUrl(m_session->yggdrasil_server).host()),
                 MessageLevel::Launcher);
    const QString part = jar + ".part";
    auto download = Net::Request::makeFile(url, part);
    download->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha256, sha256));
    m_job.reset(new NetJob("authlib-injector", APPLICATION->network()));
    m_job->setAskRetry(false);
    m_job->addNetAction(download);
    connect(m_job.get(), &Task::finished, this, [this, download, part, jar, version, sha256] {
        if (download->error() != QNetworkReply::NoError || fileSha256(part) != sha256) {
            QFile::remove(part);
            useCachedOrFail(download->errorString());
            return;
        }
        QFile::remove(jar);
        if (!QFile::rename(part, jar)) {
            useCachedOrFail(tr("it could not be saved"));
            return;
        }
        QJsonObject newInfo{ { "version", version }, { "sha256", sha256 } };
        if (auto saved = FS::write(FS::PathCombine(m_dir, kInfoName), QJsonDocument(newInfo).toJson()); !saved)
            qWarning() << "Couldn't save authlib-injector info:" << saved.error();
        succeedWith(jar);
    });
    m_job->start();
}

void EnsureAuthlibInjector::useCachedOrFail(const QString& reason)
{
    const QString jar = FS::PathCombine(m_dir, kJarName);
    const auto info = QJsonDocument::fromJson(FS::read(FS::PathCombine(m_dir, kInfoName)).value_or(QByteArray())).object();
    const QString sha256 = info.value("sha256").toString();
    if (!sha256.isEmpty() && fileSha256(jar) == sha256) {
        emit logLine(tr("Couldn't check for a newer authlib-injector (%1). Using the one already downloaded.").arg(reason),
                     MessageLevel::Warning);
        succeedWith(jar);
        return;
    }
    if (m_session->yggdrasil_optional) {
        // an offline skin is a nicety: start the game with the default skin instead
        emit logLine(tr("Couldn't download authlib-injector (%1), so you play with the default skin.").arg(reason), MessageLevel::Warning);
        m_session->yggdrasil_server.clear();
        m_session->yggdrasil_metadata.clear();
        emitSucceeded();
        return;
    }
    emit logLine(tr("Couldn't download authlib-injector (%1). Skin server accounts need it to start the game.").arg(reason),
                 MessageLevel::Fatal);
    emitFailed(tr("Couldn't download authlib-injector, which %1 accounts need. Check your internet connection and try again.")
                   .arg(QUrl(m_session->yggdrasil_server).host()));
}

void EnsureAuthlibInjector::succeedWith(const QString& jar)
{
    m_session->authlib_injector_jar = QDir::toNativeSeparators(QFileInfo(jar).absoluteFilePath());
    emitSucceeded();
}

bool EnsureAuthlibInjector::abort()
{
    if (m_job)
        m_job->abort();
    emitAborted();
    return true;
}
