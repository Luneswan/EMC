# EMC

Play, mod and host Minecraft from one place.

**[Download EMC for Windows](https://github.com/Luneswan/EMC/releases/latest/download/EMC-Setup.exe)** and run it. It installs for you only, needs no admin rights, and adds EMC to the Start menu and desktop. Java is downloaded by itself when a game needs it.

Coming from another launcher? EMC finds Prism Launcher, PolyMC, MultiMC and UltimMC on your computer by itself (installed or portable) and brings your instances, worlds, groups, accounts and Java over, without copying anything twice. The other launcher keeps its own files. A copy kept somewhere unusual can be added from **Folders → Import from Prism, MultiMC or UltimMC…**.

Prefer no installer? Take `EMC-windows-x64.zip` from the [release page](https://github.com/Luneswan/EMC/releases/latest), unzip it anywhere and run `EMC.exe`.

## What it does

- **Play right away.** Sign in with Microsoft, or play offline with just a name. A first-run wizard sets everything up, and one click installs Minecraft.
- **Mods and modpacks.** Browse Modrinth and CurseForge, install mods, resource packs, shaders and whole modpacks, and keep them updated.
- **Servers.** Create a Java or Bedrock server (or both together through Geyser), or add one you already have.
  - Vanilla, Paper, Purpur, Folia, Fabric, Forge, NeoForge and Arclight.
  - Plugins and mods from Modrinth, installed with what they need.
  - Memory and settings tuned to your computer automatically, or set by hand.
  - A console, a player list and a settings editor.
- **Friends anywhere.** Open a port on your router automatically, or use a free playit.gg tunnel when your connection cannot accept incoming players. A built-in test tells you which one works on your connection.
- **Runs in the background.** Keep servers running in the system tray, and start EMC with Windows if you like.
- **Always up to date.** Before a game starts, EMC updates its mods from Modrinth and CurseForge to the newest build for that Minecraft version and loader, adding any new mods they need. Before a server starts, its plugins and mods are updated from Modrinth the same way (releases only, never downgraded, and a Bukkit plugin is never swapped for a Fabric mod).
- **Java sorted for you.** Each game version gets the Java it needs, downloaded and switched automatically when you change versions.
- **Works with other launchers.** Import instances from Prism Launcher, MultiMC, CurseForge, Modrinth, ATLauncher and Technic. Exported instances open in Prism Launcher, PolyMC and MultiMC too.

## Build from source (Windows)

You need [MSYS2](https://www.msys2.org/) with the UCRT64 environment:

```
pacman -S mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,ccache,qt6-base,qt6-svg,qt6-imageformats,qt6-networkauth,qt6-tools,qt6-5compat,extra-cmake-modules,cmark,libarchive,tomlplusplus,zlib,qrencode}
```

You also need a JDK 17 on `PATH`. Then run `build-emc.sh` from an MSYS2 shell. The finished app goes to `install/`, with every DLL it needs next to `EMC.exe`. To build the installer, run `makensis -NOCD ../build/program_info/win_install.nsi` from `install/`; it writes `EMC-Setup.exe`.

## Contributing

EMC is a personal project and does not accept pull requests or issues. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

EMC is free software under the [GNU GPL v3](LICENSE). It is based on Prism Launcher, PolyMC and MultiMC; their copyright notices are kept in [COPYING.md](COPYING.md) and in the source files.
