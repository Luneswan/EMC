// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QSet>
#include <QString>

namespace ServerNetwork {
// This computer's address on the home network (what friends on the same Wi-Fi type), or empty.
QString lanAddress();
// True when nothing on this computer is listening on the port (TCP for Java, UDP for Bedrock).
bool portAvailable(int port, bool udp);
// The first available port from `start` on that is not in `taken`.
int nextAvailablePort(int start, bool udp, const QSet<int>& taken = {});
}  // namespace ServerNetwork
