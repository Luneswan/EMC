#!/usr/bin/env bash
# Build EMC with the MSYS2 UCRT64 toolchain (no vcpkg). Run from MSYS2 bash -l.
set -euo pipefail
export MSYSTEM=UCRT64
export USERPROFILE="${USERPROFILE:-C:\Users\anasa}"
export CCACHE_DIR="/t/MC/emc/.ccache"
export JAVA_HOME="/c/Program Files/Microsoft/jdk-17.0.18.8-hotspot"
export PATH=/ucrt64/bin:/usr/bin:"$JAVA_HOME/bin":/c/Windows/System32:/c/Windows
# Keep a stray MSVC Qt (C:\Qt) out of the configure.
unset Qt6_DIR QTDIR QT_DIR CMAKE_PREFIX_PATH || true
cd "$(dirname "$0")"
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/ucrt64 \
  -DCMAKE_IGNORE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" \
  -DCMAKE_INSTALL_PREFIX="$PWD/install" \
  -DLauncher_QT_VERSION_MAJOR=6 \
  -DLauncher_ENABLE_JAVA_DOWNLOADER=ON \
  -DENABLE_LTO=OFF \
  -DLauncher_WARNINGS_AS_ERRORS=OFF \
  -DLauncher_APP_BINARY_NAME=EMC \
  -DLauncher_UPDATER_GITHUB_REPO= \
  -DLauncher_NEWS_RSS_URL= \
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
  -DLauncher_BUILD_PLATFORM=win-mingw \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build -j"$(nproc)"
cmake --install build
# cmake --install resolves EMC.exe's DLLs but not those of Qt plugins (qwebp needs libwebp, which
# needs libsharpyuv). Copy any missing MSYS2 DLL that an installed DLL needs, until none are left.
for _ in 1 2 3; do
  find install -name '*.dll' -print0 | xargs -0 objdump -p 2>/dev/null | sed -n 's/^\s*DLL Name: //p' | sort -u |
    while read -r dll; do
      [ -e "install/$dll" ] || [ ! -e "/ucrt64/bin/$dll" ] || cp -v "/ucrt64/bin/$dll" install/
    done
done
