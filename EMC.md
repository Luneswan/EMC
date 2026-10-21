# EMC

EMC is a Minecraft launcher built on Prism Launcher. It has these changes:

- **Offline and Microsoft accounts both work on their own.** You don't need
  a Microsoft account that owns Minecraft before adding an offline one.
  - An offline account launches the full game in offline mode, not the demo.
  - The first-run wizard offers both *Sign in with Microsoft* and *Play
    Offline*.
- **The EMC Chrome theme** is the default. It has black surfaces, white type
  and brushed-silver controls. Colour is left to content (instance icons and
  skins). The default icon set is `flat_white`.
- **A cleaner first run.**
  - Language comes from the system, so there is no 60-row language page.
  - The pages use modern wizard style with a centred sign-in page.
  - The buttons are labelled Continue, Back and Get Started.
- **A new logo:** a chrome isometric cube on a black squircle. It is used for
  the exe, the window icons and the empty state.
- **The launcher cat** replaces Prism's background cat. The toolbar cat
  button toggles it.
  - It lives on the window. The tops of buttons, instance tiles and the
    status bar are ledges it sits, walks and naps on. When the list scrolls,
    it rides along with its tile.
  - It peers over edges and usually turns back. Sometimes it steps off and
    falls. Now and then it leaps up to a ledge in reach.
  - Click it to pet it. Drag it to pick it up by the scruff; it swings like
    a pendulum. Let go to drop it. If your hand is moving when you let go,
    it is thrown.
  - It always falls with gravity, rights itself in the air and lands on its
    feet with a squash and a puff of dust. Hard landings leave it dizzy.
  - Double-click it to make it hop.
  - Right-click it for Pet, Nap/Wake Up, Jump Down, Coat (Tuxedo, Silver,
    Midnight, Snow, Ginger) and Hide Cat.
  - Its eyes follow the cursor and it blinks. It naps after 30 s alone.
  - Its position and coat are remembered.
  - It stops animating while the window is minimised or hidden.
- **Branding:**
  - The binary is `EMC.exe` and its data is in `%APPDATA%\EMC`, separate
    from any Prism install.
  - There is no news feed.
  - The self-updater is off, so EMC never replaces itself with stock Prism.
- **Build fixes for GCC/MinGW:**
  - `-mguard=cf` is only passed to Clang.
  - `Launcher_WARNINGS_AS_ERRORS` can be turned off.
  - The `.rc` file builds as its own object library.

EMC still uses Prism's Microsoft login client ID and CurseForge API key. Before
you distribute EMC publicly, register your own and set them at configure time.

## Build (Windows, MSYS2 UCRT64)

These packages are needed:

```
pacman -S mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,ccache,qt6-base,qt6-svg,qt6-imageformats,qt6-networkauth,qt6-tools,qt6-5compat,extra-cmake-modules,cmark,libarchive,tomlplusplus,zlib,qrencode}
```

The build also needs a JDK on PATH; `build-emc.sh` uses Microsoft JDK 17.
Then run:

```
C:\Users\anasa\msys64\usr\bin\bash -lc /t/MC/emc/build-emc.sh
```

The output goes to `install/`. It already includes the Qt and MinGW DLLs next
to `EMC.exe`, so the folder runs anywhere.

## License

EMC is GPL-3.0, the same as Prism Launcher. Upstream license headers are kept.
