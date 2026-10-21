// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

class QWidget;
class MinecraftInstance;

namespace ContentAutoUpdate {
// Brings an instance's mods up to date from Modrinth and CurseForge before it launches, without asking.
// Only versions for the instance's Minecraft version and loader are taken, and new required mods come
// with them. Runs at most every few hours per instance, and only when the "AutoUpdateContent" setting is on.
void updateInstance(QWidget* parent, MinecraftInstance* instance);
}  // namespace ContentAutoUpdate
