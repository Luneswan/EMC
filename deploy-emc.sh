#!/usr/bin/env bash
# Make install/ self-contained: Qt plugins via windeployqt, then every MinGW DLL
# EMC.exe (and those plugins) load from /ucrt64. Run from MSYS2 bash -l after build-emc.sh.
set -euo pipefail
export PATH=/ucrt64/bin:/usr/bin
cd "$(dirname "$0")/install"

windeployqt6 --release --no-translations --no-system-d3d-compiler --no-opengl-sw EMC.exe

# Copy every /ucrt64 DLL that any exe or DLL here needs, until nothing new turns up.
while :; do
    added=0
    while read -r dll; do
        name=$(basename "$dll")
        if [ ! -e "$name" ]; then
            cp "$dll" .
            added=1
        fi
    done < <(find . -maxdepth 3 \( -name '*.exe' -o -name '*.dll' \) -print0 |
        xargs -0 ldd 2>/dev/null | awk '/\/ucrt64\// {print $3}' | sort -u)
    [ "$added" = 0 ] && break
done

# OpenSSL / TLS backend for Qt networking.
for f in /ucrt64/bin/libssl-3-x64.dll /ucrt64/bin/libcrypto-3-x64.dll; do
    [ -e "$f" ] && cp -n "$f" .
done
echo "deployed: $(ls *.dll | wc -l) DLLs"
