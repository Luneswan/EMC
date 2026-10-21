#!/bin/sh
# Installs (or updates) EMC on macOS or Linux:
#
#   curl -fsSL https://raw.githubusercontent.com/Luneswan/EMC/main/install.sh | sh
#
# macOS: puts EMC.app in Applications. Linux: puts the AppImage in ~/.local/bin and adds EMC to the
# app menu. Nothing needs administrator rights, and running it again updates EMC to the newest version.
set -eu

REPO="Luneswan/EMC"
BASE="https://github.com/$REPO/releases/latest/download"

say() { printf '\033[1m%s\033[0m\n' "$*"; }
fail() { printf 'EMC install: %s\n' "$*" >&2; exit 1; }

command -v curl >/dev/null 2>&1 || fail "curl is needed to download EMC."
TMP="$(mktemp -d)"
MOUNT=""
cleanup() {
    if [ -n "$MOUNT" ]; then hdiutil detach "$MOUNT" -quiet 2>/dev/null || true; fi
    rm -rf "$TMP"
}
trap cleanup EXIT

install_macos() {
    say "Downloading EMC for macOS..."
    curl -fL --progress-bar "$BASE/EMC-macOS.dmg" -o "$TMP/EMC.dmg" || fail "the download failed."

    MOUNT="$(hdiutil attach -nobrowse -readonly "$TMP/EMC.dmg" | awk -F '\t' '/\/Volumes\//{print $NF}' | tail -n 1)"
    [ -d "$MOUNT/EMC.app" ] || fail "the download did not contain EMC.app."

    DEST="/Applications"
    [ -w "$DEST" ] || DEST="$HOME/Applications"
    mkdir -p "$DEST"
    if pgrep -xq EMC; then
        say "Closing EMC to update it..."
        osascript -e 'quit app "EMC"' >/dev/null 2>&1 || true
        sleep 3
    fi
    rm -rf "$DEST/EMC.app"
    ditto "$MOUNT/EMC.app" "$DEST/EMC.app"
    hdiutil detach "$MOUNT" -quiet || true
    MOUNT=""
    # Downloaded with curl, so macOS has not flagged it; clear the flag in case it was set anyway.
    xattr -dr com.apple.quarantine "$DEST/EMC.app" 2>/dev/null || true

    say "EMC is installed in $DEST. Opening it..."
    open "$DEST/EMC.app"
}

install_linux() {
    case "$(uname -m)" in
        x86_64 | amd64) ARCH=x86_64 ;;
        aarch64 | arm64) ARCH=aarch64 ;;
        *) fail "EMC is built for x86_64 and ARM64 Linux; this computer is $(uname -m)." ;;
    esac

    BIN="$HOME/.local/bin"
    APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
    ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"
    mkdir -p "$BIN" "$APPS" "$ICONS"

    say "Downloading EMC for Linux ($ARCH)..."
    curl -fL --progress-bar "$BASE/EMC-Linux-$ARCH.AppImage" -o "$TMP/EMC.AppImage" || fail "the download failed."
    chmod +x "$TMP/EMC.AppImage"
    mv -f "$TMP/EMC.AppImage" "$BIN/EMC.AppImage"
    curl -fsSL "https://raw.githubusercontent.com/$REPO/main/program_info/io.github.luneswan.EMC_256.png" \
        -o "$ICONS/io.github.luneswan.EMC.png" || true

    # AppImages mount themselves with FUSE. Without it, EMC unpacks itself each start instead (slower).
    EXEC="\"$BIN/EMC.AppImage\""
    if ! command -v fusermount >/dev/null 2>&1 && ! command -v fusermount3 >/dev/null 2>&1; then
        EXEC="env APPIMAGE_EXTRACT_AND_RUN=1 $EXEC"
        say "Note: FUSE is not installed, so EMC will start a little slower."
        say "      To fix that: sudo apt install fuse3   (or your distribution's fuse package)"
    fi

    cat > "$APPS/io.github.luneswan.EMC.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=EMC
Comment=Play and host Minecraft
Exec=$EXEC %U
Icon=io.github.luneswan.EMC
Terminal=false
Categories=Game;
Keywords=game;minecraft;mc;launcher;server;
StartupWMClass=EMC
MimeType=x-scheme-handler/curseforge;x-scheme-handler/modrinth;
EOF
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" >/dev/null 2>&1 || true

    say "EMC is installed. Find it in your app menu, or run: $BIN/EMC.AppImage"
    case ":$PATH:" in *":$BIN:"*) ;; *) say "(Add $BIN to your PATH to start it by name.)" ;; esac
    if [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
        if [ "${EXEC#env }" != "$EXEC" ]; then
            APPIMAGE_EXTRACT_AND_RUN=1 nohup "$BIN/EMC.AppImage" >/dev/null 2>&1 &
        else
            nohup "$BIN/EMC.AppImage" >/dev/null 2>&1 &
        fi
    fi
}

case "$(uname -s)" in
    Darwin) install_macos ;;
    Linux) install_linux ;;
    *) fail "this installer is for macOS and Linux. On Windows, download EMC-Setup.exe from https://github.com/$REPO/releases/latest" ;;
esac
