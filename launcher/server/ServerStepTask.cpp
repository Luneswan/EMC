// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerStepTask.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonParseError>
#include <QProcess>

#include "Application.h"
#include "net/ChecksumValidator.h"
#include "net/NetJob.h"

bool ServerStepTask::abort()
{
    m_aborted = true;
    if (m_current && m_current->canAbort()) {
        m_current->abort();
    }
    emitAborted();
    return true;
}

void ServerStepTask::runSubTask(Task::Ptr task, std::function<void()> next)
{
    m_current = task;
    connect(task.get(), &Task::succeeded, this, [this, next] {
        // Queued: the next step may replace m_current, which must not happen inside its own signal.
        QMetaObject::invokeMethod(
            this,
            [this, next] {
                if (!m_aborted && isRunning()) {
                    next();
                }
            },
            Qt::QueuedConnection);
    });
    connect(task.get(), &Task::failed, this, [this](const QString& reason) {
        if (isRunning()) {
            emitFailed(reason);
        }
    });
    connect(task.get(), &Task::progress, this, &ServerStepTask::setProgress);
    connect(task.get(), &Task::details, this, &ServerStepTask::setDetails);
    connect(task.get(), &Task::stepProgress, this, &ServerStepTask::propagateStepProgress);
    task->start();
}

void ServerStepTask::fetch(const QUrl& url, std::function<void(const QByteArray&)> next)
{
    auto job = makeShared<NetJob>(QString("EMC::Fetch"), APPLICATION->network());
    auto [request, data] = Net::Request::makeByteArray(url);
    job->addNetAction(request);
    job->setAskRetry(false);
    // The byte array lives as long as the request, which the job (m_current) keeps until the next step.
    runSubTask(job, [next, data] {
        const QByteArray bytes = *data;  // copied: `next` may start another step and free the job
        next(bytes);
    });
}

void ServerStepTask::fetchJson(const QUrl& url, std::function<void(const QJsonDocument&)> next)
{
    fetch(url, [this, url, next](const QByteArray& bytes) {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError) {
            emitFailed(tr("Unexpected answer from %1: %2").arg(url.host(), error.errorString()));
            return;
        }
        next(doc);
    });
}

void ServerStepTask::download(const QUrl& url,
                              const QString& path,
                              const QString& hash,
                              QCryptographicHash::Algorithm algorithm,
                              std::function<void()> next)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    auto job = makeShared<NetJob>(QString("EMC::Download"), APPLICATION->network());
    auto request = Net::Request::makeFile(url, path);
    if (!hash.isEmpty()) {
        request->addValidator(new Net::ChecksumValidator(algorithm, QByteArray::fromHex(hash.toLatin1())));
    }
    job->addNetAction(request);
    job->setAskRetry(false);
    setStatus(tr("Downloading %1").arg(QFileInfo(path).fileName()));
    runSubTask(job, next);
}

void ServerStepTask::runProgram(const QString& program, const QStringList& args, const QString& workDir, std::function<void()> next)
{
    auto* process = new QProcess(this);
    process->setWorkingDirectory(workDir);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        const auto lines = QString::fromLocal8Bit(process->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
        if (!lines.isEmpty()) {
            setDetails(lines.last().trimmed());
        }
    });
    connect(process, &QProcess::finished, this, [this, process, program, next](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (m_aborted || !isRunning()) {
            return;
        }
        if (status != QProcess::NormalExit || code != 0) {
            emitFailed(tr("%1 exited with code %2").arg(QFileInfo(program).fileName()).arg(code));
            return;
        }
        next();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, program](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && isRunning()) {
            process->deleteLater();
            emitFailed(tr("Could not start %1").arg(program));
        }
    });
    process->start(program, args);
}
