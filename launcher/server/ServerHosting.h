// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include "ServerInstance.h"

// Keeps a server public while it runs: opens the router's port (UPnP) or runs its playit.gg tunnel
// when it starts, and closes them again when it stops.
namespace ServerHosting {
void statusChanged(ServerInstance* server, ServerInstance::Status previous, ServerInstance::Status now);
// Applies a new public mode to a running server at once.
void modeChanged(ServerInstance* server, const QString& oldMode);
}  // namespace ServerHosting
