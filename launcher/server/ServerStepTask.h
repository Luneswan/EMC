// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QUrl>

#include <functional>

#include "tasks/Task.h"

// A task written as a chain of asynchronous steps: fetch some JSON, then download a file, then run a
// sub-task... Each helper runs one step and calls the next on success; any failure fails the task.
class ServerStepTask : public Task {
    Q_OBJECT
   public:
    explicit ServerStepTask(QObject* parent = nullptr) : Task(parent) { setAbortable(true); }

    bool canAbort() const override { return true; }
    bool abort() override;

   protected:
    void fetch(const QUrl& url, std::function<void(const QByteArray&)> next);
    void fetchJson(const QUrl& url, std::function<void(const QJsonDocument&)> next);
    // `hash` may be empty to skip verification.
    void download(const QUrl& url,
                  const QString& path,
                  const QString& hash,
                  QCryptographicHash::Algorithm algorithm,
                  std::function<void()> next);
    void runSubTask(Task::Ptr task, std::function<void()> next);
    // Runs a program to completion (for example a mod loader installer), streaming its output into details.
    void runProgram(const QString& program, const QStringList& args, const QString& workDir, std::function<void()> next);

    Task::Ptr m_current;
    bool m_aborted = false;
};
