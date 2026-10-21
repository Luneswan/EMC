// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ServerOptimizer.h"

#include <QObject>
#include <QThread>

#include <algorithm>

#include "HardwareInfo.h"
#include "ServerProperties.h"

namespace ServerOptimizer {

Machine Machine::current()
{
    Machine m;
    if (const auto ram = HardwareInfo::totalRamMiB(); ram > 0) {
        m.ramMiB = ram;
    }
    m.threads = std::max(1, QThread::idealThreadCount());
    return m;
}

static int roundTo256(double mib)
{
    return std::max(512, int(mib / 256.0 + 0.5) * 256);
}

Plan plan(const Machine& machine, bool mods, bool bedrock)
{
    Plan p;
    const double ramGiB = machine.ramMiB / 1024.0;

    // The laptop still has to run Windows (and often the game itself) next to the server, so the
    // server never gets more than half of the RAM on small machines, and the share grows slowly.
    double heapGiB;
    if (ramGiB <= 4.5) {
        heapGiB = 1.0;
    } else if (ramGiB <= 8.5) {
        heapGiB = mods ? 3.0 : 2.0;
    } else if (ramGiB <= 16.5) {
        heapGiB = mods ? 6.0 : 4.0;
    } else if (ramGiB <= 32.5) {
        heapGiB = mods ? 10.0 : 6.0;
    } else {
        heapGiB = mods ? 14.0 : 10.0;
    }
    heapGiB = std::min(heapGiB, ramGiB * 0.5);
    p.maxMemMiB = roundTo256(heapGiB * 1024);
    // A fixed heap (min == max) avoids resize pauses once there is room for it.
    p.minMemMiB = ramGiB > 8.5 ? p.maxMemMiB : roundTo256(p.maxMemMiB / 2.0);
    p.useAikarFlags = p.maxMemMiB >= 2048;

    // View distance is the biggest CPU/RAM cost per player.
    if (machine.threads <= 2 || ramGiB <= 4.5) {
        p.viewDistance = 6;
        p.simulationDistance = 5;
        p.maxPlayers = 6;
    } else if (machine.threads <= 4 || ramGiB <= 8.5) {
        p.viewDistance = 8;
        p.simulationDistance = 6;
        p.maxPlayers = 10;
    } else if (machine.threads <= 8 || ramGiB <= 16.5) {
        p.viewDistance = 10;
        p.simulationDistance = 8;
        p.maxPlayers = 20;
    } else {
        p.viewDistance = 12;
        p.simulationDistance = 10;
        p.maxPlayers = 30;
    }
    if (bedrock) {
        p.bedrockThreads = std::clamp(machine.threads / 2, 2, 8);
        p.summary = QObject::tr("%1 CPU threads, %2 GB RAM: view distance %3 chunks, %4 players, %5 worker threads.")
                        .arg(machine.threads)
                        .arg(ramGiB, 0, 'f', 0)
                        .arg(p.viewDistance)
                        .arg(p.maxPlayers)
                        .arg(p.bedrockThreads);
    } else {
        p.summary = QObject::tr("%1 CPU threads, %2 GB RAM: %3 GB for the server, view distance %4 chunks, %5 players.")
                        .arg(machine.threads)
                        .arg(ramGiB, 0, 'f', 0)
                        .arg(p.maxMemMiB / 1024.0, 0, 'f', 1)
                        .arg(p.viewDistance)
                        .arg(p.maxPlayers);
    }
    return p;
}

QStringList aikarFlags(int maxMemMiB)
{
    const bool big = maxMemMiB >= 12 * 1024;
    return {
        "-XX:+UseG1GC",
        "-XX:+ParallelRefProcEnabled",
        "-XX:MaxGCPauseMillis=200",
        "-XX:+UnlockExperimentalVMOptions",
        "-XX:+DisableExplicitGC",
        "-XX:+AlwaysPreTouch",
        QString("-XX:G1NewSizePercent=%1").arg(big ? 40 : 30),
        QString("-XX:G1MaxNewSizePercent=%1").arg(big ? 50 : 40),
        QString("-XX:G1HeapRegionSize=%1").arg(big ? "16M" : "8M"),
        QString("-XX:G1ReservePercent=%1").arg(big ? 15 : 20),
        "-XX:G1HeapWastePercent=5",
        "-XX:G1MixedGCCountTarget=4",
        QString("-XX:InitiatingHeapOccupancyPercent=%1").arg(big ? 20 : 15),
        "-XX:G1MixedGCLiveThresholdPercent=90",
        "-XX:G1RSetUpdatingPauseTimePercent=5",
        "-XX:SurvivorRatio=32",
        "-XX:+PerfDisableSharedMem",
        "-XX:MaxTenuringThreshold=1",
        "-Dusing.aikars.flags=https://mcflags.emc.gs",
        "-Daikars.new.flags=true",
    };
}

void apply(const Plan& plan, ServerProperties& props, bool bedrock)
{
    if (bedrock) {
        props.setValue("view-distance", plan.viewDistance + 4);  // Bedrock's default is 32; anything from 5 up is valid
        props.setValue("tick-distance", std::clamp(plan.simulationDistance / 2, 4, 6));
        props.setValue("max-players", plan.maxPlayers);
        if (plan.bedrockThreads > 0) {
            props.setValue("max-threads", plan.bedrockThreads);
        }
        return;
    }
    props.setValue("view-distance", plan.viewDistance);
    props.setValue("simulation-distance", plan.simulationDistance);
    props.setValue("max-players", plan.maxPlayers);
}

}  // namespace ServerOptimizer
