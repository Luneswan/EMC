// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerInstance.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRegularExpression>

#include "Application.h"
#include "FileSystem.h"
#include "ServerContent.h"
#include "ServerFirewall.h"
#include "ServerHosting.h"
#include "ServerJavaTask.h"
#include "ServerNetwork.h"
#include "ServerOptimizer.h"
#include "ServerProperties.h"
#include "launch/LogModel.h"
#include "settings/INISettingsObject.h"

ServerInstance::ServerInstance(QString id, QString metaDir, QObject* parent)
    : QObject(parent), m_id(std::move(id)), m_metaDir(std::move(metaDir)), m_log(new LogModel(this))
{
    m_settings = std::make_unique<INISettingsObject>(FS::PathCombine(m_metaDir, "emc-server.cfg"));
    auto* s = m_settings.get();
    s->registerSetting("Name", m_id);
    s->registerSetting("Software", "vanilla");
    s->registerSetting("McVersion", "");
    s->registerSetting("Build", "");
    s->registerSetting("LaunchType", "jar");  // jar | args | exe
    s->registerSetting("LaunchTarget", "server.jar");
    s->registerSetting("JavaName", "");
    s->registerSetting("JavaMajor", 21);
    s->registerSetting("JavaPath", "");  // an override; empty uses the downloaded runtime
    s->registerSetting("MemoryAuto", true);
    s->registerSetting("MaxMemMiB", 2048);
    s->registerSetting("MinMemMiB", 1024);
    s->registerSetting("AikarFlags", true);
    s->registerSetting("JvmArgs", "");
    s->registerSetting("AutoStart", false);
    s->registerSetting("RestartOnCrash", true);
    s->registerSetting("Crossplay", false);
    s->registerSetting("BedrockPort", 19132);
    s->registerSetting("PublicMode", "off");  // off | upnp | playit
    s->registerSetting("PublicAddress", "");  // the router's internet address when PublicMode is upnp
    s->registerSetting("PlayitJava", "");
    s->registerSetting("PlayitBedrock", "");
    s->registerSetting("CustomDomain", "");       // a domain the user owns, e.g. play.example.com
    s->registerSetting("CustomDomainOk", false);  // its DNS records were checked and point here
    s->registerSetting("DuckDnsName", "");        // a free name.duckdns.org that EMC keeps updated
    s->registerSetting("DuckDnsToken", "");
    s->registerSetting("GameDir", "");  // set for servers imported in place
    s->registerSetting("Created", QDateTime::currentDateTime().toString(Qt::ISODate));

    m_log->setMaxLines(20000);
    m_log->setStopOnOverflow(false);
    m_log->setLineWrap(true);
    m_log->setColorLines(true);

    m_killTimer.setSingleShot(true);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_process && m_process->state() == LoggedProcess::Running) {
            appendLauncherLine(tr("The server did not stop in time; ending it."), MessageLevel::Warning);
            m_process->kill();
        }
    });
}

ServerInstance::~ServerInstance()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

QString ServerInstance::gameDir() const
{
    const QString custom = m_settings->get("GameDir").toString();
    return custom.isEmpty() ? FS::PathCombine(m_metaDir, "server") : custom;
}

QString ServerInstance::name() const
{
    return m_settings->get("Name").toString();
}

void ServerInstance::setName(const QString& name)
{
    m_settings->set("Name", name);
    emit changed();
}

QString ServerInstance::software() const
{
    return m_settings->get("Software").toString();
}

const ServerSoftware::Info* ServerInstance::softwareInfo() const
{
    return ServerSoftware::find(software());
}

QString ServerInstance::mcVersion() const
{
    return m_settings->get("McVersion").toString();
}

QString ServerInstance::propertiesPath() const
{
    return FS::PathCombine(gameDir(), "server.properties");
}

int ServerInstance::port() const
{
    ServerProperties props;
    props.load(propertiesPath());
    return props.intValue("server-port", isBedrock() ? 19132 : 25565);
}

bool ServerInstance::crossplay() const
{
    return !isBedrock() && m_settings->get("Crossplay").toBool();
}

int ServerInstance::bedrockPort() const
{
    return crossplay() ? m_settings->get("BedrockPort").toInt() : 0;
}

QString ServerInstance::typeLabel() const
{
    const auto* info = softwareInfo();
    const QString name = info ? (isBedrock() ? tr("Bedrock") : info->name) : software();
    return mcVersion().isEmpty() ? name : QString("%1 %2").arg(name, mcVersion());
}

QString ServerInstance::statusText() const
{
    switch (m_status) {
        case Status::Stopped:
            return tr("Stopped");
        case Status::Preparing:
            return tr("Preparing");
        case Status::Starting:
            return tr("Starting");
        case Status::Running:
            return tr("Running");
        case Status::Stopping:
            return tr("Stopping");
        case Status::Crashed:
            return tr("Crashed");
    }
    return {};
}

int ServerInstance::effectiveMaxMemMiB() const
{
    if (m_settings->get("MemoryAuto").toBool()) {
        const auto* info = softwareInfo();
        return ServerOptimizer::plan(ServerOptimizer::Machine::current(), info && info->mods, isBedrock()).maxMemMiB;
    }
    return m_settings->get("MaxMemMiB").toInt();
}

void ServerInstance::setStatus(Status status)
{
    if (m_status == status) {
        return;
    }
    const Status previous = m_status;
    m_status = status;
    if (status == Status::Stopped || status == Status::Crashed) {
        if (!m_players.isEmpty()) {
            m_players.clear();
            emit playersChanged();
        }
    }
    ServerHosting::statusChanged(this, previous, status);
    emit statusChanged(status);
}

QString ServerInstance::publicMode() const
{
    return m_settings->get("PublicMode").toString();
}

QString ServerInstance::customDomain() const
{
    return m_settings->get("CustomDomainOk").toBool() ? m_settings->get("CustomDomain").toString() : QString();
}

QString ServerInstance::duckDnsHost() const
{
    const QString name = m_settings->get("DuckDnsName").toString();
    return name.isEmpty() ? QString() : name + ".duckdns.org";
}

QString ServerInstance::serverProgram() const
{
    if (m_settings->get("LaunchType").toString() == "exe") {
        return QDir(gameDir()).absoluteFilePath(m_settings->get("LaunchTarget").toString());
    }
    const QString custom = m_settings->get("JavaPath").toString();
    if (!custom.isEmpty() && QFileInfo::exists(custom)) {
        return custom;
    }
    return ServerJavaTask::installedJava(m_settings->get("JavaName").toString());
}

QStringList ServerInstance::publicAddresses() const
{
    QStringList out;
    const QString mode = publicMode();
    const QString custom = customDomain();
    // A checked domain has an SRV record for the Java port, so it needs no port. Bedrock players
    // always type the port themselves.
    const auto withPort = [](const QString& host, int port, int standard) {
        return port == standard ? host : QString("%1:%2").arg(host).arg(port);
    };
    if (mode == "upnp") {
        QString host = duckDnsHost();
        if (host.isEmpty()) {
            host = m_settings->get("PublicAddress").toString();
        }
        if (!host.isEmpty()) {
            if (isBedrock()) {
                out << withPort(custom.isEmpty() ? host : custom, port(), 19132);
            } else {
                out << (custom.isEmpty() ? withPort(host, port(), 25565) : custom);
                if (crossplay()) {
                    out << QString("%1:%2").arg(custom.isEmpty() ? host : custom).arg(bedrockPort());
                }
            }
        }
    } else if (mode == "playit") {
        const QString java = m_settings->get("PlayitJava").toString();
        const QString bedrock = m_settings->get("PlayitBedrock").toString();
        if (!isBedrock() && !java.isEmpty()) {
            out << (custom.isEmpty() ? java : custom);
        }
        if ((isBedrock() || crossplay()) && !bedrock.isEmpty()) {
            out << (custom.isEmpty() ? bedrock : QString("%1:%2").arg(custom, bedrock.section(':', -1)));
        }
    }
    return out;
}

void ServerInstance::appendLauncherLine(const QString& line, MessageLevel level)
{
    m_log->append(level, QString("[EMC] %1").arg(line));
}

void ServerInstance::start()
{
    if (isActive()) {
        return;
    }
    m_lastError.clear();
    m_restartAfterStop = false;
    if (!QDir(gameDir()).exists()) {
        m_lastError = tr("The server folder is missing: %1").arg(gameDir());
        appendLauncherLine(m_lastError, MessageLevel::Error);
        setStatus(Status::Crashed);
        return;
    }
    if (isBedrock()) {
        launchProcess({});
        return;
    }

    // Java servers: use the override if it exists, else the matching downloaded runtime, else fetch it.
    const QString custom = m_settings->get("JavaPath").toString();
    if (!custom.isEmpty() && QFileInfo::exists(custom)) {
        launchProcess(custom);
        return;
    }
    const QString javaName = m_settings->get("JavaName").toString();
    if (const QString java = ServerJavaTask::installedJava(javaName); !java.isEmpty()) {
        launchProcess(java);
        return;
    }
    setStatus(Status::Preparing);
    appendLauncherLine(tr("Downloading Java %1 for this server...").arg(m_settings->get("JavaMajor").toInt()));
    auto task = makeShared<ServerJavaTask>(javaName, m_settings->get("JavaMajor").toInt());
    m_prepare = task;
    connect(task.get(), &Task::succeeded, this, [this, task] {
        m_prepare.reset();
        launchProcess(task->javaPath());
    });
    connect(task.get(), &Task::failed, this, [this](const QString& reason) {
        m_prepare.reset();
        m_lastError = reason;
        appendLauncherLine(tr("Could not get Java: %1").arg(reason), MessageLevel::Error);
        setStatus(Status::Crashed);
    });
    task->start();
}

QStringList ServerInstance::buildArguments(const QString& java, QString* program) const
{
    const QString type = m_settings->get("LaunchType").toString();
    const QString target = m_settings->get("LaunchTarget").toString();
    if (type == "exe") {
        *program = QDir(gameDir()).absoluteFilePath(target);
        return {};
    }
    *program = java;

    QStringList args;
    int maxMem = m_settings->get("MaxMemMiB").toInt();
    int minMem = m_settings->get("MinMemMiB").toInt();
    bool aikar = m_settings->get("AikarFlags").toBool();
    if (m_settings->get("MemoryAuto").toBool()) {
        const auto* info = softwareInfo();
        const auto plan = ServerOptimizer::plan(ServerOptimizer::Machine::current(), info && info->mods, false);
        maxMem = plan.maxMemMiB;
        minMem = plan.minMemMiB;
        aikar = plan.useAikarFlags;
    }
    args << QString("-Xms%1M").arg(std::min(minMem, maxMem)) << QString("-Xmx%1M").arg(maxMem);
    if (aikar) {
        args << ServerOptimizer::aikarFlags(maxMem);
    }
    const QString extra = m_settings->get("JvmArgs").toString().trimmed();
    if (!extra.isEmpty()) {
        args << QProcess::splitCommand(extra);
    }
    if (type == "args") {
        // A loader's own argument list (Forge/NeoForge run script), one argument per line. Its
        // @.../win_args.txt or unix_args.txt is swapped for this system's, which the installer also wrote.
#ifdef Q_OS_WIN
        const QString native = "win_args.txt", foreign = "unix_args.txt";
#else
        const QString native = "unix_args.txt", foreign = "win_args.txt";
#endif
        for (QString arg : target.split('\n', Qt::SkipEmptyParts)) {
            if (arg.startsWith('@') && arg.endsWith(foreign)) {
                const QString swapped = arg.left(arg.size() - foreign.size()) + native;
                if (QFileInfo::exists(FS::PathCombine(gameDir(), swapped.mid(1)))) {
                    arg = swapped;
                }
            }
            args << arg;
        }
    } else {
        args << "-jar" << target;
    }
    args << "nogui";
    return args;
}

void ServerInstance::syncGeyserPort()
{
    // Geyser's config lives under plugins/ (Paper family) or config/ (Fabric, NeoForge).
    const int port = bedrockPort();
    if (port <= 0) {
        return;
    }
    const QStringList candidates{ "plugins/Geyser-Spigot/config.yml", "config/Geyser-Fabric/config.yml",
                                  "config/Geyser-NeoForge/config.yml" };
    for (const auto& relative : candidates) {
        const QString path = FS::PathCombine(gameDir(), relative);
        QFile file(path);
        if (!file.exists()) {
            continue;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
        file.close();
        bool inBedrock = false;
        bool changed = false;
        for (auto& line : lines) {
            if (!line.startsWith(' ') && !line.startsWith('#') && !line.trimmed().isEmpty()) {
                inBedrock = line.startsWith("bedrock:");
                continue;
            }
            static const QRegularExpression portLine("^(\\s+port:\\s*)(\\d+)(.*)$");
            if (const auto m = portLine.match(line); inBedrock && m.hasMatch()) {
                if (m.captured(2).toInt() != port) {
                    line = m.captured(1) + QString::number(port) + m.captured(3);
                    changed = true;
                }
                break;
            }
        }
        if (changed && file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(lines.join('\n').toUtf8());
        }
        return;
    }
    // Geyser has not run yet. Only a non-default port needs a config before its first start.
    if (port == 19132) {
        return;
    }
    const auto* info = softwareInfo();
    QString relative = "plugins/Geyser-Spigot/config.yml";
    if (info && info->mods && !info->plugins) {
        relative = software() == "fabric" ? "config/Geyser-Fabric/config.yml" : "config/Geyser-NeoForge/config.yml";
    }
    const QString path = FS::PathCombine(gameDir(), relative);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QString("# Written by EMC; Geyser adds its other settings on first start.\nbedrock:\n  port: %1\n"
                           "java:\n  auth-type: floodgate\n")
                       .arg(port)
                       .toUtf8());
    }
}

void ServerInstance::ensurePortsFree()
{
    // Another program (an SSH tunnel, a second launcher's server) may hold the port. Move to the
    // next free one instead of letting the server fail with "FAILED TO BIND TO PORT".
    ServerProperties props;
    props.setUnicodeEscapes(!isBedrock());  // Bedrock reads UTF-8 and does not understand \u escapes
    props.load(propertiesPath());
    bool changed = false;
    auto check = [&](const QString& key, int fallback, bool udp) {
        const int current = props.intValue(key, fallback);
        if (ServerNetwork::portAvailable(current, udp)) {
            return current;
        }
        const int next = ServerNetwork::nextAvailablePort(current + 1, udp);
        props.setValue(key, next);
        changed = true;
        appendLauncherLine(tr("Port %1 is used by another program, so this server uses port %2 instead.").arg(current).arg(next),
                           MessageLevel::Warning);
        return next;
    };
    if (isBedrock()) {
        const int port = check("server-port", 19132, true);
        if (props.intValue("server-portv6", 19133) == port) {
            props.setValue("server-portv6", port + 1);
            changed = true;
        }
        check("server-portv6", port + 1, true);
    } else {
        check("server-port", 25565, false);
        if (crossplay()) {
            const int bedrock = bedrockPort();
            if (!ServerNetwork::portAvailable(bedrock, true)) {
                const int next = ServerNetwork::nextAvailablePort(bedrock + 1, true);
                m_settings->set("BedrockPort", next);
                appendLauncherLine(tr("Bedrock port %1 is used by another program, so crossplay uses port %2 instead.")
                                       .arg(bedrock)
                                       .arg(next),
                                   MessageLevel::Warning);
            }
        }
    }
    if (changed) {
        props.save(propertiesPath());
    }
}

void ServerInstance::launchProcess(const QString& java)
{
    ensurePortsFree();
    if (crossplay() && !isBedrock()) {
        syncGeyserPort();
    }
    QString program;
    const QStringList args = buildArguments(java, &program);
    if (program.isEmpty() || !QFileInfo::exists(program)) {
        m_lastError = isBedrock() ? tr("%1 was not found in the server folder.").arg(ServerSoftware::bedrockExecutable()) : tr("Java was not found.");
        appendLauncherLine(m_lastError, MessageLevel::Error);
        setStatus(Status::Crashed);
        return;
    }
    // Bring Modrinth plugins and mods up to date first; if Modrinth cannot be reached the server starts as it is.
    const auto* info = softwareInfo();
    if (!m_contentChecked && !isBedrock() && info && !info->modrinthLoaders.isEmpty() &&
        APPLICATION->settings()->get("AutoUpdateContent").toBool()) {
        // plugins/ takes plugin loaders and mods/ mod loaders; hybrid servers have both.
        QStringList pluginLoaders, modLoaders;
        for (const auto& loader : info->modrinthLoaders) {
            (ServerContent::isModLoaderSet({ loader }) ? modLoaders : pluginLoaders) << loader;
        }
        QList<ModrinthServerUpdateTask::Folder> folders;
        for (const auto& [sub, loaders] : { qMakePair(QString("plugins"), pluginLoaders), qMakePair(QString("mods"), modLoaders) }) {
            if (!loaders.isEmpty() && QDir(FS::PathCombine(gameDir(), sub)).exists()) {
                folders << qMakePair(FS::PathCombine(gameDir(), sub), loaders);
            }
        }
        if (!folders.isEmpty()) {
            m_contentChecked = true;
            setStatus(Status::Preparing);
            appendLauncherLine(tr("Checking plugins and mods for updates..."));
            auto task = makeShared<ModrinthServerUpdateTask>(folders, mcVersion());
            m_prepare = task;
            connect(task.get(), &Task::finished, this, [this, task, java] {
                m_prepare.reset();
                const auto updated = task->updated();
                if (!updated.isEmpty()) {
                    appendLauncherLine(tr("Updated: %1").arg(updated.join(", ")));
                } else if (!task->wasSuccessful()) {
                    appendLauncherLine(tr("Could not check for updates: %1").arg(task->failReason()), MessageLevel::Warning);
                }
                if (m_status == Status::Preparing) {
                    launchProcess(java);
                } else {
                    m_contentChecked = false;  // stopped while checking
                }
            });
            task->start();
            return;
        }
    }
    // Let the program through Windows Firewall first, so other devices can join.
    if (!m_firewallChecked) {
        m_firewallChecked = true;
        setStatus(Status::Preparing);
        QPointer<ServerInstance> guard(this);
        ServerFirewall::ensure(program, [guard, java] {
            if (!guard) {
                return;
            }
            if (guard->m_status == Status::Preparing) {
                guard->launchProcess(java);
            } else {
                guard->m_firewallChecked = false;  // stopped while asking
            }
        });
        return;
    }
    m_firewallChecked = false;
    m_contentChecked = false;

    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
    }
    m_process = new LoggedProcess(isBedrock() ? QStringConverter::Utf8 : QStringConverter::System, this);
    m_process->setWorkingDirectory(gameDir());
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove("JAVA_TOOL_OPTIONS");  // a stray global setting must not change the server's memory or flags
#ifdef Q_OS_LINUX
    if (isBedrock()) {
        env.insert("LD_LIBRARY_PATH", gameDir());  // the Linux Bedrock server loads libraries from its folder
    }
#endif
    m_process->setProcessEnvironment(env);
    connect(m_process, &LoggedProcess::log, this, &ServerInstance::onLog);
    connect(m_process, &LoggedProcess::stateChanged, this, &ServerInstance::onProcessState);

    appendLauncherLine(tr("Starting %1 (%2)").arg(name(), typeLabel()));
    appendLauncherLine(QString("%1 %2").arg(QFileInfo(program).fileName(), args.join(' ')));
    m_startedAt = QDateTime::currentDateTime();
    setStatus(Status::Starting);
    m_process->start(program, args);
}

void ServerInstance::onLog(const QStringList& lines, MessageLevel level)
{
    static const QRegularExpression javaJoin("\\]: ([^\\s\\[\\]]+) joined the game");
    static const QRegularExpression javaLeave("\\]: ([^\\s\\[\\]]+) left the game");
    static const QRegularExpression bedrockJoin("Player connected: (.+?), xuid");
    static const QRegularExpression bedrockLeave("Player disconnected: (.+?), xuid");
    // Paper and Arclight colour their output with terminal escape codes, which the console cannot show.
    static const QRegularExpression ansi("\x1B\\[[0-9;?]*[A-Za-z]");

    for (auto line : lines) {
        line.remove(ansi);
        MessageLevel lineLevel = level;
        if (line.contains("/WARN]") || line.contains(" WARN]")) {
            lineLevel = MessageLevel::Warning;
        } else if (line.contains("/ERROR]") || line.contains(" ERROR]") || line.contains("/FATAL]")) {
            lineLevel = MessageLevel::Error;
        } else if (lineLevel == MessageLevel::StdOut || lineLevel == MessageLevel::StdErr) {
            lineLevel = MessageLevel::Info;
        }
        m_log->append(lineLevel, line);
        if (line.contains("FAILED TO BIND TO PORT")) {
            m_lastError = tr("Another program is using this server's port. Change the port in Settings, or close that program.");
        }
        if (line.contains("Jar does not contain plugin.yml")) {
            appendLauncherLine(tr("A jar in the plugins folder is not a Bukkit plugin (it may be made only for Paper, or be a mod). "
                                  "Remove it in Plugins, or put mods in Mods."));
        }

        if (m_status == Status::Starting &&
            ((line.contains("]: Done (") && line.contains("For help, type")) || line.contains("Server started."))) {
            setStatus(Status::Running);
            emit ready();
        }
        bool playersChanged = false;
        if (auto m = javaJoin.match(line); m.hasMatch()) {
            m_players.append(m.captured(1));
            playersChanged = true;
        } else if (auto m2 = bedrockJoin.match(line); m2.hasMatch()) {
            m_players.append(m2.captured(1));
            playersChanged = true;
        } else if (auto m3 = javaLeave.match(line); m3.hasMatch()) {
            m_players.removeOne(m3.captured(1));
            playersChanged = true;
        } else if (auto m4 = bedrockLeave.match(line); m4.hasMatch()) {
            m_players.removeOne(m4.captured(1));
            playersChanged = true;
        }
        if (playersChanged) {
            m_players.removeDuplicates();
            emit this->playersChanged();
        }
    }
}

void ServerInstance::onProcessState(LoggedProcess::State state)
{
    switch (state) {
        case LoggedProcess::Running:
            break;
        case LoggedProcess::FailedToStart:
            m_lastError = tr("The server process could not be started.");
            appendLauncherLine(m_lastError, MessageLevel::Error);
            setStatus(Status::Crashed);
            break;
        case LoggedProcess::Finished:
        case LoggedProcess::Crashed:
        case LoggedProcess::Aborted: {
            m_killTimer.stop();
            const int code = m_process ? m_process->exitCode() : 0;
            const bool wanted = m_status == Status::Stopping;
            if (wanted || (state == LoggedProcess::Finished && code == 0)) {
                appendLauncherLine(tr("Server stopped."));
                setStatus(Status::Stopped);
                if (m_restartAfterStop) {
                    m_restartAfterStop = false;
                    QTimer::singleShot(500, this, &ServerInstance::start);
                }
                break;
            }
            m_lastError = tr("The server stopped unexpectedly (exit code %1).").arg(code);
            appendLauncherLine(m_lastError, MessageLevel::Error);
            setStatus(Status::Crashed);

            // Restart a crashed server, but give up if it keeps crashing.
            const auto now = QDateTime::currentDateTime();
            m_recentCrashes.append(now);
            while (!m_recentCrashes.isEmpty() && m_recentCrashes.first().secsTo(now) > 600) {
                m_recentCrashes.removeFirst();
            }
            if (m_settings->get("RestartOnCrash").toBool() && m_startedAt.secsTo(now) > 30) {
                if (m_recentCrashes.size() <= 3) {
                    appendLauncherLine(tr("Restarting in 5 seconds..."));
                    QTimer::singleShot(5000, this, [this] {
                        if (m_status == Status::Crashed) {
                            start();
                        }
                    });
                } else {
                    appendLauncherLine(tr("The server crashed 3 times in 10 minutes; not restarting it again."), MessageLevel::Warning);
                }
            }
            break;
        }
        default:
            break;
    }
}

void ServerInstance::stop()
{
    if (m_status == Status::Preparing && m_prepare) {
        // Stopped first: aborting finishes the task at once, and its handler must not start the server.
        setStatus(Status::Stopped);
        auto prepare = m_prepare;
        m_prepare.reset();
        prepare->abort();
        return;
    }
    if (!m_process || m_process->state() != LoggedProcess::Running) {
        setStatus(Status::Stopped);
        return;
    }
    if (m_status == Status::Stopping) {
        return;
    }
    setStatus(Status::Stopping);
    appendLauncherLine(tr("Stopping the server (saving the world)..."));
    m_process->write("stop\n");
    m_killTimer.start(60 * 1000);
}

void ServerInstance::restart()
{
    if (!isActive()) {
        start();
        return;
    }
    m_restartAfterStop = true;
    stop();
}

void ServerInstance::kill()
{
    m_restartAfterStop = false;
    if (m_process && m_process->state() == LoggedProcess::Running) {
        setStatus(Status::Stopping);
        m_process->kill();
    } else {
        stop();
    }
}

void ServerInstance::sendCommand(const QString& command)
{
    const QString c = command.trimmed();
    if (c.isEmpty() || !m_process || m_process->state() != LoggedProcess::Running) {
        return;
    }
    m_log->append(MessageLevel::Launcher, QString("> %1").arg(c));
    if (c == "stop" || c == "/stop") {
        stop();
        return;
    }
    m_process->write((c.startsWith('/') ? c.mid(1) : c).toUtf8() + "\n");
}
