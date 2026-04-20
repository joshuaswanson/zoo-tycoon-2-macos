#!/bin/bash
# Zoo Tycoon 2 Ultimate Collection - macOS Launcher
# Runs via Wine with D3D9 proxy (GPU spoof + mode injection + OpenGL renderer)

# Rebrand process + menu bar as "Zoo Tycoon 2" (read by our winemac.drv patch).
export WINE_APP_NAME="Zoo Tycoon 2"

cd "$(dirname "$0")/Game Files"

# Kill any existing Wine processes
wineserver -k 2>/dev/null
sleep 1

# Start the auto-click helper (handles EULA, Warning, and Error dialogs).
# WINE_HIDE_DOCK=1 tells our winemac.drv patch to hide this helper's Dock icon.
WINE_HIDE_DOCK=1 WINE_APP_NAME= WINEDLLOVERRIDES="d3d9=n,b;d3d9_real=b" wine click_continue.exe &

sleep 2

# Launch the game
WINEDLLOVERRIDES="d3d9=n,b;d3d9_real=b" wine ZT.exe

# Cleanup
wineserver -k 2>/dev/null
