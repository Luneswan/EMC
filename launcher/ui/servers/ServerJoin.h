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

class QWidget;
class ServerInstance;

namespace ServerJoin {
// True when the Bedrock game is installed on this PC (EMC can only open it, not provide it).
bool bedrockInstalled();
// "Play on this server": opens the right game with this server already selected. Java servers
// use (or create) an instance with the server's Minecraft version; Bedrock opens the Bedrock game.
void play(ServerInstance* server, QWidget* parent);
// The same for any address, such as a hosted server. An empty mcVersion means it is unknown.
void join(QWidget* parent, const QString& name, bool bedrock, const QString& host, int port, const QString& mcVersion,
          const QString& loaderUid = {});
}  // namespace ServerJoin
