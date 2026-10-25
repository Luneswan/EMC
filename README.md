# EMC

Play, mod and host Minecraft from one place.

## Install

**Windows:** **[download EMC-Setup.exe](https://github.com/Luneswan/EMC/releases/latest/download/EMC-Setup.exe)** and run it. It installs for you only, needs no admin rights, and adds EMC to the Start menu and desktop.

**macOS and Linux:** open Terminal and paste this line:

```sh
curl -fsSL https://raw.githubusercontent.com/Luneswan/EMC/main/install.sh | sh
```

On a Mac it puts EMC in Applications; on Linux it adds EMC to your app menu. No admin password is needed, and running the same line again updates EMC.

Java is downloaded by itself whenever a game or server needs it, on every system.

<details>
<summary>Prefer to download the files yourself?</summary>

- **Windows, no installer:** `EMC-windows-x64.zip`. Unzip it anywhere and run `EMC.exe`.
- **macOS:** `EMC-macOS.dmg` (Intel and Apple Silicon, macOS 13 or newer). Open it and drag EMC to Applications. EMC is not signed with a paid Apple developer ID yet, so the first time macOS says it cannot check it: open **System Settings → Privacy & Security** and click **Open Anyway**. The install line above avoids this.
- **Linux:** `EMC-Linux-x86_64.AppImage` or `EMC-Linux-aarch64.AppImage`. Make it executable (`chmod +x`) and run it.

All of them are on the [release page](https://github.com/Luneswan/EMC/releases/latest).
</details>

Coming from another launcher? EMC finds Prism Launcher, PolyMC, MultiMC and UltimMC on your computer by itself (installed or portable) and brings your instances, worlds, groups, accounts and Java over, without copying anything twice. The other launcher keeps its own files. A copy kept somewhere unusual can be added from **Folders → Import from Prism, MultiMC or UltimMC…**.

## What it does

- **Play right away.** Sign in with Microsoft, or play offline with just a name. A first-run wizard sets everything up, and one click installs Minecraft.
- **Custom skins with Ely.by.** Sign in with a free [Ely.by](https://ely.by) account and your skin and cape show in game, and to other players on servers that use Ely.by. Accounts that use two-factor sign-in get a code prompt. Other skin servers that support authlib-injector (LittleSkin, Drasl, your own) work too. EMC downloads and checks [authlib-injector](https://github.com/yushijinhun/authlib-injector) for you and never saves your password.
  - Offline accounts can wear any Ely.by skin too, without signing in: pick an Ely.by player's name in **Manage Skins** (your own name is used by default). The account keeps its name and UUID, so worlds and servers are untouched.
- **Mods and modpacks.** Browse Modrinth and CurseForge, install mods, resource packs, shaders and whole modpacks, and keep them updated.
- **Servers.** Create a Java or Bedrock server (or both together through Geyser), or add one you already have.
  - Vanilla, Paper, Purpur, Folia, Fabric, Forge, NeoForge and Arclight.
  - Plugins and mods from Modrinth, installed with what they need.
  - Memory and settings tuned to your computer automatically, or set by hand.
  - A console, a player list and a settings editor.
- **Friends anywhere.** Open a port on your router automatically, or use a free playit.gg tunnel when your connection cannot accept incoming players. A built-in test tells you which one works on your connection.
- **Runs in the background.** Keep servers running in the system tray (the menu bar on a Mac), and start EMC when you sign in if you like.
- **Updates itself.** New versions download in the background. Click **Restart to update**, or EMC updates the next time you close it. The portable zip links to the download page instead.
- **Windows, macOS and Linux.** Everything works on all three, with two exceptions that come from the makers of those programs: Mojang makes the Bedrock server only for Windows and Intel/AMD Linux (elsewhere, Bedrock players join a Java server through Geyser), and playit.gg makes its tunnel only for Windows and Linux (a Mac can still open a router port).
- **Always up to date.** Before a game starts, EMC updates its mods from Modrinth and CurseForge to the newest build for that Minecraft version and loader, adding any new mods they need. Before a server starts, its plugins and mods are updated from Modrinth the same way (releases only, never downgraded, and a Bukkit plugin is never swapped for a Fabric mod).
- **Java sorted for you.** Each game version gets the Java it needs, downloaded and switched automatically when you change versions.
- **Works with other launchers.** Import instances from Prism Launcher, MultiMC, CurseForge, Modrinth, ATLauncher and Technic. Exported instances open in Prism Launcher, PolyMC and MultiMC too.

## Build from source

macOS and Linux builds are made by GitHub Actions (`.github/workflows/build.yml`), which uses the `linux` and `macos_universal` CMake presets with Qt 6 and vcpkg.

### Windows

You need [MSYS2](https://www.msys2.org/) with the UCRT64 environment:

```
pacman -S mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,ccache,qt6-base,qt6-svg,qt6-imageformats,qt6-networkauth,qt6-tools,qt6-5compat,extra-cmake-modules,cmark,libarchive,tomlplusplus,zlib,qrencode}
```

You also need a JDK 17 on `PATH`. Then run `build-emc.sh` from an MSYS2 shell. The finished app goes to `install/`, with every DLL it needs next to `EMC.exe`. To build the installer, run `makensis -NOCD ../build/program_info/win_install.nsi` from `install/`; it writes `EMC-Setup.exe`.

## Contributing

EMC is a personal project and does not accept pull requests or issues. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

EMC is free software under the [GNU GPL v3](LICENSE). It is based on Prism Launcher, PolyMC and MultiMC; their copyright notices are kept in [COPYING.md](COPYING.md) and in the source files.
