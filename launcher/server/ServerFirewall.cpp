// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerFirewall.h"

#include <QCoreApplication>
#include <QDir>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QTimer>

#include "Application.h"
#include "settings/SettingsObject.h"

#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

namespace {
ServerFirewall::Asker& asker()
{
    static ServerFirewall::Asker instance;
    return instance;
}

#ifdef Q_OS_WIN
const QString kRuleName = "EMC Minecraft server";
const QString kRequestFlag = "--emc-firewall-allow";

QString normalized(const QString& program)
{
    return QDir::toNativeSeparators(QDir::cleanPath(program)).toLower();
}

QStringList declined()
{
    return APPLICATION->settings()->get("ServerFirewallDeclined").toString().split('|', Qt::SkipEmptyParts);
}

void setDeclined(const QString& program, bool on)
{
    QStringList list = declined();
    list.removeAll(normalized(program));
    if (on) {
        list.append(normalized(program));
    }
    APPLICATION->settings()->set("ServerFirewallDeclined", list.join('|'));
}
#endif
}  // namespace

void ServerFirewall::setAsker(Asker value)
{
    asker() = std::move(value);
}

void ServerFirewall::check(const QString& program, std::function<void(State)> done)
{
#ifdef Q_OS_WIN
    // netsh can list rules without admin rights. A Block rule (left by an earlier "Cancel" in
    // Windows' own prompt) wins over any Allow rule, so it counts as blocked.
    auto* process = new QProcess(qApp);
    QObject::connect(process, &QProcess::finished, qApp, [process, program, done](int code) {
        process->deleteLater();
        if (code != 0) {
            done(State::Unknown);
            return;
        }
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput()).replace("\r\n", "\n");
        const QString target = normalized(program);
        bool allowed = false;
        bool blocked = false;
        for (const auto& rule : output.split("\n\n", Qt::SkipEmptyParts)) {
            if (!rule.toLower().contains(target)) {
                continue;
            }
            bool enabled = true;
            bool block = false;
            bool allow = false;
            bool everyNetwork = false;  // Windows' own prompt makes rules for one profile only
            const bool ours = rule.contains(kRuleName);  // whatever language netsh speaks
            for (const auto& line : rule.split('\n')) {
                const QString value = line.section(':', 1).trimmed();
                if (line.startsWith("Enabled:")) {
                    enabled = value.compare("Yes", Qt::CaseInsensitive) == 0;
                } else if (line.startsWith("Action:")) {
                    block = value.compare("Block", Qt::CaseInsensitive) == 0;
                    allow = value.compare("Allow", Qt::CaseInsensitive) == 0;
                } else if (line.startsWith("Profiles:")) {
                    everyNetwork = value.contains("Private") && value.contains("Public");
                }
            }
            blocked = blocked || (enabled && block);
            allowed = allowed || (enabled && !block && (ours || (allow && everyNetwork)));
        }
        done(blocked ? State::Blocked : allowed ? State::Allowed : State::Missing);
    });
    QObject::connect(process, &QProcess::errorOccurred, qApp, [process, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {  // no finished() follows
            process->deleteLater();
            done(State::Unknown);
        }
    });
    process->start("netsh", { "advfirewall", "firewall", "show", "rule", "name=all", "dir=in", "verbose" });
#else
    Q_UNUSED(program)
    done(State::Allowed);
#endif
}

void ServerFirewall::ensure(const QString& program, std::function<void()> then)
{
#ifdef Q_OS_WIN
    // Checked once per run: listing every firewall rule takes a second or so.
    static QSet<QString> settled;
    const QString key = normalized(program);
    if (!asker() || settled.contains(key) || declined().contains(key)) {
        then();
        return;
    }
    check(program, [program, key, then](State state) {
        if (state == State::Allowed || state == State::Unknown) {
            settled.insert(key);
            then();
            return;
        }
        asker()(program, [program, key, then](bool yes) {
            if (!yes) {
                setDeclined(program, true);
                then();
                return;
            }
            allow(program, [key, then](bool ok) {
                if (ok) {
                    settled.insert(key);
                }
                then();
            });
        });
    });
#else
    Q_UNUSED(program)
    then();
#endif
}

void ServerFirewall::allow(const QString& program, std::function<void(bool ok)> done)
{
#ifdef Q_OS_WIN
    // Run EMC.exe itself elevated, so the UAC prompt shows EMC's name and logo.
    const std::wstring file = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    const std::wstring params = QString("%1 \"%2\"").arg(kRequestFlag, QDir::toNativeSeparators(program)).toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = file.c_str();
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess) {
        done(false);  // the user said no to the UAC prompt
        return;
    }
    setDeclined(program, false);
    auto* timer = new QTimer(qApp);
    timer->setInterval(200);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, handle = info.hProcess, done] {
        if (WaitForSingleObject(handle, 0) != WAIT_OBJECT_0) {
            return;
        }
        DWORD code = 1;
        GetExitCodeProcess(handle, &code);
        CloseHandle(handle);
        timer->deleteLater();
        done(code == 0);
    });
    timer->start();
#else
    Q_UNUSED(program)
    done(true);
#endif
}

int ServerFirewall::runElevatedRequest()
{
#ifdef Q_OS_WIN
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return -1;
    }
    if (argc != 3 || QString::fromWCharArray(argv[1]) != kRequestFlag) {
        LocalFree(argv);
        return -1;
    }
    const QString program = QDir::toNativeSeparators(QString::fromWCharArray(argv[2]));
    LocalFree(argv);
    const QString lower = program.toLower();
    if (!(lower.endsWith("\\java.exe") || lower.endsWith("\\javaw.exe") || lower.endsWith("\\bedrock_server.exe"))) {
        return 2;  // only ever let a server program through
    }

    const auto run = [](const QString& arguments) {
        std::wstring command = QString("netsh.exe %1").arg(arguments).toStdWString();
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            return DWORD(1);
        }
        WaitForSingleObject(process.hProcess, 30000);
        DWORD code = 1;
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
        return code;
    };
    // Drop every inbound rule for this program first: Windows saves a Block rule when its own
    // prompt is cancelled, and a Block rule always wins over an Allow rule.
    run(QString("advfirewall firewall delete rule name=all dir=in program=\"%1\"").arg(program));
    return int(run(QString("advfirewall firewall add rule name=\"%1\" dir=in action=allow enable=yes profile=any program=\"%2\"")
                       .arg(kRuleName, program)));
#else
    return -1;
#endif
}
