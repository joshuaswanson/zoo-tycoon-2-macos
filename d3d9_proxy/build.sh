#!/bin/bash
# Build all proxy DLLs and tools
set -e
cd "$(dirname "$0")"

CC=i686-w64-mingw32-gcc
CFLAGS="-O2"

echo "Building d3d9.dll (proxy)..."
$CC -shared -o d3d9.dll d3d9_proxy.c cds_hook.c -ld3d9 -luser32 -Wl,--kill-at $CFLAGS

echo "Building click_continue.exe..."
$CC -o click_continue.exe click_continue.c -luser32 -lgdi32 $CFLAGS

echo "Building list_windows.exe..."
$CC -o list_windows.exe list_windows.c -luser32 -lgdi32 $CFLAGS

echo "Done! Deploy with:"
echo "  cp d3d9.dll click_continue.exe \"../Game Files/\""
