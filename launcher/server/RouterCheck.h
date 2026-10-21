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

#include <functional>

// Answers "can friends on the internet reach a server on this computer?" in plain terms.
namespace RouterCheck {

struct Report {
    QString lanAddress;     // this computer on the home network
    bool upnp = false;      // the router accepts automatic port forwarding
    QString routerName;
    QString routerAddress;  // the router's internet side, as the router reports it
    QString publicAddress;  // what the internet sees
    // The router's internet address is not the public one: the provider shares one public address
    // between many homes (CGNAT), or there is a second router in front. Port forwarding cannot work.
    bool sharedAddress = false;
    bool carrierNat = false;  // the router's address is in 100.64.0.0/10 (CGNAT for certain)
};

// Runs the whole router test (UPnP, router address, public address). `done` runs once.
void run(std::function<void(const Report&)> done);

enum class Reachable { Yes, No, Unknown };
// Asks an outside service whether a Minecraft server answers at host:port.
void testReachable(const QString& host, int port, bool bedrock, std::function<void(Reachable, QString)> done);

// True for addresses that cannot be reached from the internet (private, CGNAT, link-local).
bool isPrivate(const QString& address);

}  // namespace RouterCheck
