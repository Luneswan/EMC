// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "PlayitAgent.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTimer>

#include "Application.h"
#include "FileSystem.h"
#include "ServerInstance.h"
#include "ServerStepTask.h"

namespace {
const char* kAgentUrl = "https://github.com/playit-cloud/playit-agent/releases/download/v0.17.1/playit-windows-x86_64-signed.exe";
const char* kAgentSha256 = "9b00d6ff7d37d1052e5ae097e1348e11deae8617cd7a8ba39d1777f2006316a3";
constexpr int kClaimIntervalMs = 2000;
constexpr int kClaimMaxAttempts = 300;  // ten minutes

// Downloads the agent and checks it against the published hash.
class PlayitDownloadTask : public ServerStepTask {
   public:
    PlayitDownloadTask(QString path) : m_path(std::move(path)) {}

   protected:
    void executeTask() override
    {
        const QString partial = m_path + ".part";
        download(QUrl(kAgentUrl), partial, kAgentSha256, QCryptographicHash::Sha256, [this, partial] {
            QFile::remove(m_path);
            if (!QFile::rename(partial, m_path)) {
                emitFailed(tr("Could not save the playit agent."));
                return;
            }
            emitSucceeded();
        });
    }

   private:
    QString m_path;
};

struct Wanted {
    QString proto;  // "tcp" or "udp", as the playit API names them
    QString type;   // "minecraft-java" or "minecraft-bedrock"
    int port;
    QString setting;  // where the public address is stored
};

QList<Wanted> wantedTunnels(const ServerInstance* server)
{
    if (server->isBedrock()) {
        return { { "udp", "minecraft-bedrock", server->port(), "PlayitBedrock" } };
    }
    QList<Wanted> list{ { "tcp", "minecraft-java", server->port(), "PlayitJava" } };
    if (server->crossplay()) {
        list.append({ "udp", "minecraft-bedrock", server->bedrockPort(), "PlayitBedrock" });
    }
    return list;
}

// The tunnel in rundata that serves `want`, or an empty object.
QJsonObject findTunnel(const QJsonArray& tunnels, const Wanted& want)
{
    for (const auto value : tunnels) {
        const auto tunnel = value.toObject();
        if (tunnel.value("proto").toString() == want.proto && tunnel.value("local_port").toInt() == want.port &&
            tunnel.value("disabled").isNull()) {
            return tunnel;
        }
    }
    return {};
}

QString publicAddress(const QJsonObject& tunnel, const Wanted& want)
{
    QString domain = tunnel.value("custom_domain").toString();
    if (domain.isEmpty()) {
        domain = tunnel.value("assigned_domain").toString();
    }
    // Java tunnels come with an SRV record, so the name alone works. Bedrock needs the port.
    if (want.type == "minecraft-java") {
        return domain;
    }
    return QString("%1:%2").arg(domain).arg(tunnel.value("port").toObject().value("from").toInt());
}
}  // namespace

PlayitAgent* PlayitAgent::instance()
{
    static auto* agent = new PlayitAgent(qApp);
    return agent;
}

PlayitAgent::PlayitAgent(QObject* parent) : QObject(parent)
{
    QFile file(FS::PathCombine(folder(), "secret.txt"));
    if (file.open(QIODevice::ReadOnly)) {
        m_secret = QString::fromUtf8(file.readAll()).trimmed();
    }
    m_claimPoll.setInterval(kClaimIntervalMs);
    connect(&m_claimPoll, &QTimer::timeout, this, &PlayitAgent::pollClaim);
}

QString PlayitAgent::folder() const
{
    return FS::PathCombine(APPLICATION->dataRoot(), "playit");
}

QString PlayitAgent::agentPath() const
{
    return FS::PathCombine(folder(), QString("playit-%1.exe").arg(kVersion));
}

QString PlayitAgent::claimUrl() const
{
    return m_code.isEmpty() ? QString() : QString("https://playit.gg/claim/%1").arg(m_code);
}

void PlayitAgent::api(const QString& path, const QJsonObject& body, std::function<void(QJsonValue, QString)> done)
{
    QNetworkRequest request(QUrl("https://api.playit.gg" + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent());
    request.setTransferTimeout(15000);
    if (!m_secret.isEmpty()) {
        request.setRawHeader("Authorization", ("Agent-Key " + m_secret).toUtf8());
    }
    auto* reply = APPLICATION->network()->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        const QString status = object.value("status").toString();
        if (status == "success") {
            done(object.value("data"), {});
        } else if (status == "fail") {
            done({}, object.value("data").toVariant().toString());
        } else if (!object.isEmpty()) {
            done({}, QJsonDocument(object.value("data").toObject()).toJson(QJsonDocument::Compact));
        } else {
            done({}, tr("playit.gg did not answer: %1").arg(reply->errorString()));
        }
    });
}

// ---------------------------------------------------------------- linking

void PlayitAgent::startLink()
{
    // The claim code is 5 random bytes in hex, as the playit agent itself makes it.
    QByteArray bytes(5, 0);
    for (auto& byte : bytes) {
        byte = char(QRandomGenerator::system()->bounded(256));
    }
    m_code = QString::fromLatin1(bytes.toHex());
    m_claimAttempts = 0;
    QDesktopServices::openUrl(QUrl(claimUrl()));
    emit linkProgress(tr("Opening playit.gg in your browser…"));
    pollClaim();
    m_claimPoll.start();
}

void PlayitAgent::cancelLink()
{
    m_claimPoll.stop();
    m_code.clear();
    emit linkProgress({});
}

void PlayitAgent::unlink()
{
    cancelLink();
    m_secret.clear();
    QFile::remove(FS::PathCombine(folder(), "secret.txt"));
    m_active.clear();
    updateProcess();
    emit linkedChanged();
}

void PlayitAgent::pollClaim()
{
    if (m_code.isEmpty()) {
        return;
    }
    if (++m_claimAttempts > kClaimMaxAttempts) {
        cancelLink();
        emit linkProgress(tr("Linking timed out. Try again."));
        return;
    }
    const QString code = m_code;
    QJsonObject body{ { "code", code }, { "agent_type", "self-managed" }, { "version", QString("playit %1").arg(kVersion) } };
    api("/claim/setup", body, [this, code](const QJsonValue& data, const QString& error) {
        if (code != m_code) {
            return;  // cancelled or restarted meanwhile
        }
        if (!error.isEmpty()) {
            if (error == "CodeExpired" || error == "InvalidCode") {
                cancelLink();
                emit linkProgress(tr("The link expired. Try again."));
            }
            return;  // a network hiccup: the next poll tries again
        }
        const QString state = data.toString();
        if (state == "WaitingForUserVisit") {
            emit linkProgress(tr("Waiting for the playit.gg page to open in your browser…"));
        } else if (state == "WaitingForUser") {
            emit linkProgress(tr("On the playit.gg page, sign in or continue as a guest, then click Accept."));
        } else if (state == "UserRejected") {
            cancelLink();
            emit linkProgress(tr("The link was declined on playit.gg."));
        } else if (state == "UserAccepted") {
            m_claimPoll.stop();
            emit linkProgress(tr("Approved. Finishing…"));
            api("/claim/exchange", QJsonObject{ { "code", code } }, [this, code](const QJsonValue& data, const QString& error) {
                if (code != m_code) {
                    return;
                }
                m_code.clear();
                const QString secret = data.toObject().value("secret_key").toString();
                if (!error.isEmpty() || secret.isEmpty()) {
                    emit linkProgress(tr("Could not finish linking: %1").arg(error));
                    return;
                }
                m_secret = secret;
                QDir().mkpath(folder());
                QFile file(FS::PathCombine(folder(), "secret.txt"));
                if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    file.write(secret.toUtf8());
                }
                emit linkProgress({});
                emit linkedChanged();
            });
        }
    });
}

// ---------------------------------------------------------------- tunnels

void PlayitAgent::prepare(ServerInstance* server, std::function<void(QString)> done)
{
    if (!isLinked()) {
        done(tr("Link playit.gg first."));
        return;
    }
    QPointer<ServerInstance> guard(server);
    api("/agents/rundata", {}, [this, guard, done](const QJsonValue& data, const QString& error) {
        if (!guard) {
            return;
        }
        if (!error.isEmpty()) {
            done(tr("playit.gg: %1").arg(error));
            return;
        }
        const auto run = data.toObject();
        const QString agentId = run.value("agent_id").toString();
        const auto tunnels = run.value("tunnels").toArray();
        const auto pending = run.value("pending").toArray();
        int creating = 0;
        for (const auto& want : wantedTunnels(guard)) {
            if (!findTunnel(tunnels, want).isEmpty()) {
                continue;
            }
            const QString name = QString("EMC %1 %2").arg(guard->name().left(24), want.proto == "tcp" ? "Java" : "Bedrock");
            const bool alreadyPending = std::any_of(pending.begin(), pending.end(), [&](const QJsonValue& p) {
                return p.toObject().value("name").toString() == name;
            });
            if (alreadyPending) {
                continue;
            }
            ++creating;
            QJsonObject origin{ { "type", "agent" },
                                { "data", QJsonObject{ { "agent_id", agentId }, { "local_ip", "127.0.0.1" }, { "local_port", want.port } } } };
            QJsonObject body{ { "name", name },
                              { "tunnel_type", want.type },
                              { "port_type", want.proto },
                              { "port_count", 1 },
                              { "origin", origin },
                              { "enabled", true },
                              { "alloc", QJsonValue::Null },
                              { "firewall_id", QJsonValue::Null },
                              { "proxy_protocol", QJsonValue::Null } };
            api("/tunnels/create", body, [](const QJsonValue&, const QString& createError) {
                if (!createError.isEmpty()) {
                    qWarning() << "playit tunnel create failed:" << createError;
                }
            });
        }
        // New tunnels take a few seconds to get an address.
        waitForTunnels(guard, creating > 0 ? 20 : 1, done);
    });
}

void PlayitAgent::waitForTunnels(ServerInstance* server, int attempts, std::function<void(QString)> done)
{
    QPointer<ServerInstance> guard(server);
    QTimer::singleShot(attempts > 1 ? 3000 : 0, this, [this, guard, attempts, done] {
        if (!guard) {
            return;
        }
        api("/agents/rundata", {}, [this, guard, attempts, done](const QJsonValue& data, const QString& error) {
            if (!guard) {
                return;
            }
            const auto tunnels = data.toObject().value("tunnels").toArray();
            bool all = error.isEmpty();
            for (const auto& want : wantedTunnels(guard)) {
                const auto tunnel = findTunnel(tunnels, want);
                if (tunnel.isEmpty() || tunnel.value("assigned_domain").toString().isEmpty()) {
                    all = false;
                    continue;
                }
                guard->settings()->set(want.setting, publicAddress(tunnel, want));
            }
            if (all) {
                done({});
            } else if (attempts > 1) {
                waitForTunnels(guard, attempts - 1, done);
            } else {
                done(error.isEmpty() ? tr("playit.gg has not assigned an address yet. Try again in a minute.") : error);
            }
        });
    });
}

// ---------------------------------------------------------------- agent process

void PlayitAgent::setServerActive(ServerInstance* server, bool active)
{
    m_active.removeAll(nullptr);
    m_active.removeAll(QPointer<ServerInstance>(server));
    if (active) {
        m_active.append(server);
    }
    updateProcess();
}

void PlayitAgent::ensureAgent(std::function<void(QString)> done)
{
    if (QFile::exists(agentPath())) {
        done({});
        return;
    }
    if (m_download) {
        return;  // already downloading; updateProcess runs again when it finishes
    }
    setAgentStatus(tr("Downloading the playit agent…"));
    auto task = makeShared<PlayitDownloadTask>(agentPath());
    m_download = task;
    connect(task.get(), &Task::finished, this, [this, task, done] {
        m_download.reset();
        done(task->wasSuccessful() ? QString() : task->failReason());
    });
    task->start();
}

void PlayitAgent::updateProcess()
{
    m_active.removeAll(nullptr);
    const bool wanted = isLinked() && !m_active.isEmpty();
    if (!wanted) {
        if (m_process) {
            m_process->disconnect(this);
            m_process->kill();
            m_process->waitForFinished(3000);
            m_process->deleteLater();
            m_process = nullptr;
        }
        setAgentStatus({});
        emit agentStateChanged();
        return;
    }
    if (agentRunning()) {
        return;
    }
    ensureAgent([this](const QString& error) {
        if (!error.isEmpty()) {
            setAgentStatus(tr("Could not download the playit agent: %1").arg(error));
            return;
        }
        if (agentRunning() || !isLinked() || m_active.isEmpty()) {
            return;
        }
        m_process = new QProcess(this);
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("PLAYIT_SECRET", m_secret);  // never on the command line, where other programs can read it
        m_process->setProcessEnvironment(env);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        m_process->setWorkingDirectory(folder());
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            for (const auto& line : QString::fromUtf8(m_process->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts)) {
                const QString text = line.trimmed();
                if (text.contains("tunnel running", Qt::CaseInsensitive) || text.contains("agent registered", Qt::CaseInsensitive)) {
                    setAgentStatus(tr("Connected to playit.gg"));
                } else if (text.contains("ERROR")) {
                    setAgentStatus(tr("playit agent: %1").arg(text.section("ERROR", 1).trimmed().left(160)));
                }
            }
        });
        // If the agent exits on its own (a crash, a lost connection), bring it back while a server needs it.
        auto restart = [this, process = m_process.data()] {
            if (m_process != process) {
                return;
            }
            m_process = nullptr;
            process->deleteLater();
            setAgentStatus(tr("The playit agent stopped. Reconnecting…"));
            emit agentStateChanged();
            QTimer::singleShot(15000, this, [this] {
                if (!agentRunning()) {
                    updateProcess();
                }
            });
        };
        connect(m_process, &QProcess::finished, this, restart);
        connect(m_process, &QProcess::errorOccurred, this, [restart](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                restart();
            }
        });
        m_process->start(agentPath(), { "--stdout", "start" });
        setAgentStatus(tr("Connecting to playit.gg…"));
        emit agentStateChanged();
    });
}

void PlayitAgent::setAgentStatus(const QString& status)
{
    if (m_agentStatus != status) {
        m_agentStatus = status;
        emit agentStateChanged();
    }
}
