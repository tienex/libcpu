#!/bin/sh
# Cross-build libnix for win32 with MinGW-w64 and run its host-op conformance test (nixtest)
# under Wine -- proves the win32 host backend works without a Windows machine. Mirrors the
# project's asmjit windows-wine check. Skips cleanly if the cross toolchain or wine is absent.
#
# Usage: run-libnix-wine.sh [build-dir]   (default build dir: /tmp/libnix-win)
set -e

# Find the cross toolchain + wine in the usual spots (Homebrew, ~/bin) when not already on PATH.
PATH="$HOME/bin:/opt/homebrew/bin:/usr/local/bin:$PATH"
export PATH

SRC="$(cd "$(dirname "$0")" && pwd)"
BUILD="${1:-/tmp/libnix-win}"

if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
	echo "SKIP: x86_64-w64-mingw32-gcc (MinGW-w64) not found"; exit 0
fi
if ! command -v wine >/dev/null 2>&1; then
	echo "SKIP: wine not found"; exit 0
fi

rm -rf "$BUILD"
mkdir -p "$BUILD"
cmake -S "$SRC" -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$SRC/CMake/mingw-w64.cmake" >/dev/null
cmake --build "$BUILD" --target nixtest >/dev/null

# Wine resolves DLLs from the executable's directory.
cp "$BUILD/nix/libnix.dll" "$BUILD/xec-compat/libxec-compat.dll" "$BUILD/"
cd "$BUILD"
WINEDEBUG=-all wine nixtest.exe
