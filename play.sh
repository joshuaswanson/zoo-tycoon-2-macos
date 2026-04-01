#!/bin/bash
# Zoo Tycoon 2 Ultimate Collection - macOS Launcher
# Runs via wine-crossover (brew install --cask wine-crossover)

GAME_DIR="$(cd "$(dirname "$0")/Game Files" && pwd)"
export WINEPREFIX="$HOME/ZooTycoon2"

cd "$GAME_DIR"
wine zt.exe "$@" 2>/dev/null &
disown

# Auto-dismiss EULA and video card warning dialogs
sleep 3
wine click_continue.exe 2>/dev/null &
disown
