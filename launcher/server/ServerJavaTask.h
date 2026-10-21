// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include "ServerStepTask.h"

// Finds the Java a Minecraft version needs and downloads it into EMC's Java folder if it is missing.
// Uses the same runtimes (and folder) as game launches, so the game and its servers share one copy.
class ServerJavaTask : public ServerStepTask {
    Q_OBJECT
   public:
    // `javaName` is Mojang's runtime name (e.g. "java-runtime-epsilon"); `major` its Java version.
    ServerJavaTask(QString javaName, int major, QObject* parent = nullptr)
        : ServerStepTask(parent), m_javaName(std::move(javaName)), m_major(major)
    {}

    QString javaPath() const { return m_path; }

    // The console java executable for a downloaded runtime, or empty if it is not there.
    static QString installedJava(const QString& javaName);

   protected:
    void executeTask() override;

   private:
    void downloadFrom(const QString& metaVersion);

    QString m_javaName;
    int m_major;
    QString m_path;
};
