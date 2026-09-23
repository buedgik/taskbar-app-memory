#!/bin/sh
# Compiles a mod the way Windhawk compiles it (arguments taken from
# UI/resources/app/extensions/windhawk/dist/extension.js), for every target
# Windhawk builds from the mod's @architecture lines, because a mistake can
# compile on one target and fail on another. Like Windhawk: no @architecture
# means x86 and x86-64, and x86-64 also means ARM64 (amd64 is x64 only).
# Check only: the DLLs go to the temp folder, Windhawk doesn't load them.
# Usage: sh compilar.sh taskbar-app-memory.wh.cpp
set -e
MOD="$1"
SRC="$(pwd)/$MOD"
WH="/c/Program Files/Windhawk"
ENGINE="$WH/Engine/1.7.3"
ID=$(sed -n 's|^// @id *||p' "$MOD" | tr -d '\r')
VER=$(sed -n 's|^// @version *||p' "$MOD" | tr -d '\r')
OPTS=$(sed -n 's|^// @compilerOptions *||p' "$MOD" | tr -d '\r')
ARCHS=$(sed -n 's|^// @architecture *||p' "$MOD" | tr -d '\r')
[ -n "$ARCHS" ] || ARCHS="x86 x86-64"
TARGETS=""
for A in $ARCHS; do
  case "$A" in
    x86) TARGETS="$TARGETS 32" ;;
    x86-64) TARGETS="$TARGETS 64 arm64" ;;
    amd64) TARGETS="$TARGETS 64" ;;
    arm64) TARGETS="$TARGETS arm64" ;;
    *) echo "unknown architecture: $A"; exit 1 ;;
  esac
done
cd "$WH/Compiler"

for ARCH in $(echo $TARGETS | tr ' ' '\n' | sort -u); do
  case "$ARCH" in
    32) TARGET=i686-w64-mingw32 ;;
    64) TARGET=x86_64-w64-mingw32 ;;
    arm64) TARGET=aarch64-w64-mingw32 ;;
  esac
  OUT="${TMPDIR:-/tmp}/$(basename "$MOD" .wh.cpp).$ARCH.dll"
  # shellcheck disable=SC2086
  ./bin/clang++.exe -std=c++23 -O2 -shared -DUNICODE -D_UNICODE \
    -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008 \
    -D__USE_MINGW_ANSI_STDIO=0 -DWH_MOD "-DWH_MOD_ID=L\"$ID\"" "-DWH_MOD_VERSION=L\"$VER\"" \
    "$ENGINE/$ARCH/windhawk.lib" -x c++ - -include windhawk_api.h \
    -target "$TARGET" -Wl,--export-all-symbols -o "$OUT" -Wall $OPTS < "$SRC"
  echo "ok ($ARCH): $OUT"
done
