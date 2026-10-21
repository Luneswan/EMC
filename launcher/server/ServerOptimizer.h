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

class ServerProperties;

// Picks server settings that suit the machine it runs on. Everything here is a starting point the
// user can override in the server's Performance page.
namespace ServerOptimizer {

struct Machine {
    quint64 ramMiB = 8192;
    int threads = 4;
    static Machine current();
};

struct Plan {
    int maxMemMiB = 2048;
    int minMemMiB = 1024;
    int viewDistance = 10;
    int simulationDistance = 10;
    int maxPlayers = 20;
    bool useAikarFlags = true;
    int bedrockThreads = 0;  // 0 = let the server decide
    QString summary;         // one human sentence explaining the choice
};

// `mods` servers get more memory; Bedrock servers do not use Java at all.
Plan plan(const Machine& machine, bool mods, bool bedrock);

// Aikar's G1 flags, tuned for the heap size (https://docs.papermc.io/paper/aikars-flags).
QStringList aikarFlags(int maxMemMiB);

// Writes the view/simulation distance and player cap into server.properties.
void apply(const Plan& plan, ServerProperties& props, bool bedrock);

}  // namespace ServerOptimizer
