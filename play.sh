#!/bin/bash
# Zoo Tycoon 2 Ultimate Collection - macOS Launcher
# Runs via wine-crossover (brew install --cask wine-crossover)

GAME_DIR="$(cd "$(dirname "$0")/Game Files" && pwd)"
export WINEPREFIX="$HOME/ZooTycoon2"
export WINEDLLOVERRIDES="d3d9=n"

cd "$GAME_DIR"
wine zt.exe "$@" 2>/dev/null &
disown

# Auto-dismiss EULA and video card warning dialogs
sleep 3
wine click_continue.exe 2>/dev/null &
disown

# Resize sync: reads macOS window size and calls Win32 SetWindowPos
# Delay to ensure wineserver is ready
sleep 5
wine resize_sync.exe 2>/dev/null &
disown

# Monitor macOS window size for D3D Present stretching
(while pgrep -qf wine-preloader 2>/dev/null; do
    swift -e '
import CoreGraphics
let o = CGWindowListOption(arrayLiteral: .optionOnScreenOnly)
if let l = CGWindowListCopyWindowInfo(o, kCGNullWindowID) as? [[String: Any]] {
    for w in l {
        let n = w["kCGWindowName"] as? String ?? ""
        let ow = w["kCGWindowOwnerName"] as? String ?? ""
        let b = w["kCGWindowBounds"] as? [String: Any] ?? [:]
        if ow.lowercased().contains("wine") && n.contains("Zoo Tycoon 2") && !n.contains("Error") && !n.contains("Warning") {
            let w2 = b["Width"] as? Int ?? 0
            let h = b["Height"] as? Int ?? 0
            try? "\(w2) \(h - 28)".write(toFile: "/tmp/zt2_winsize", atomically: true, encoding: .utf8)
            break
        }
    }
}' 2>/dev/null
    sleep 0.3
done) &
disown
