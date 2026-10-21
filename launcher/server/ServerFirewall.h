// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QString>
#include <QStringList>
#include <functional>

// Lets a server's program (java.exe, bedrock_server.exe) through Windows Firewall before it first
// listens. EMC adds the rule itself through a UAC prompt for EMC.exe, so people see EMC's name and
// logo instead of Windows asking about "OpenJDK Platform binary". Does nothing on other systems.
namespace ServerFirewall {
enum class State { Allowed, Missing, Blocked, Unknown };

// Shows the "let other devices join?" question; calls answer(true) to add the rule.
using Asker = std::function<void(const QString& program, std::function<void(bool allow)> answer)>;
void setAsker(Asker asker);

// Checks the rules for program in the background.
void check(const QString& program, std::function<void(State)> done);
// Before a server starts: asks once if the program is not let through yet, then calls then().
void ensure(const QString& program, std::function<void()> then);
// Adds the rule now (one UAC prompt). done(false) when the user cancels it.
void allow(const QString& program, std::function<void(bool ok)> done);

// Runs inside the elevated EMC.exe started by allow(). Returns the exit code, or -1 when the
// command line is not a firewall request.
int runElevatedRequest();
}  // namespace ServerFirewall
