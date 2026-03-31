#!/bin/bash
# Zoo Tycoon 2 Ultimate Collection - macOS Launcher
# To quit: Cmd+Option+Esc -> Force Quit "wine-preloader"

cd "$(dirname "$0")/Game Files"

# Kill any existing Wine processes
wineserver -k 2>/dev/null
sleep 1

# Start the auto-click helper (handles EULA, Warning, and Error dialogs)
WINEDLLOVERRIDES="d3d9=n,b;d3d9_real=b" wine click_continue.exe &

sleep 2

# Launch the game
WINEDLLOVERRIDES="d3d9=n,b;d3d9_real=b" wine ZT.exe

# Cleanup
wineserver -k 2>/dev/null
