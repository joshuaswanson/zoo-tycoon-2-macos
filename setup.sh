#!/bin/bash
# One-shot setup for Zoo Tycoon 2 Ultimate Collection on macOS.
# Idempotent: safe to re-run. Skips steps that are already done.
set -e

cd "$(dirname "$0")"

GREEN="\033[32m"
YELLOW="\033[33m"
RED="\033[31m"
NC="\033[0m"

say()  { printf "${GREEN}[setup]${NC} %s\n" "$*"; }
warn() { printf "${YELLOW}[setup]${NC} %s\n" "$*"; }
die()  { printf "${RED}[setup]${NC} %s\n" "$*"; exit 1; }

# 1. Homebrew
if ! command -v brew >/dev/null 2>&1; then
    die "Homebrew not found. Install from https://brew.sh and re-run."
fi

# 2. Dependencies via Homebrew
install_brew() {
    local kind="$1" pkg="$2" check="$3"
    if eval "$check" >/dev/null 2>&1; then
        say "$pkg already installed, skipping."
    else
        say "Installing $pkg..."
        if [ "$kind" = "cask" ]; then
            brew install --cask "$pkg"
        else
            brew install "$pkg"
        fi
    fi
}

install_brew cask    wine-crossover "[ -d '/Applications/Wine Crossover.app' ]"
install_brew formula mingw-w64      "command -v i686-w64-mingw32-gcc"
install_brew formula winetricks     "command -v winetricks"

# 3. Wine registry + d3dx9 (once)
SETUP_MARKER="$HOME/.wine/.zt2_setup_done"
if [ -f "$SETUP_MARKER" ]; then
    say "Wine registry + d3dx9 already configured, skipping."
else
    say "Configuring Wine registry..."
    wine reg add "HKCU\\Software\\Wine\\Direct3D"    /v renderer        /t REG_SZ /d gl       /f >/dev/null 2>&1
    wine reg add "HKCU\\Software\\Wine\\Direct3D"    /v VideoMemorySize /t REG_SZ /d 512      /f >/dev/null 2>&1
    wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v d3d9            /t REG_SZ /d native  /f >/dev/null 2>&1
    wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v d3d9_real       /t REG_SZ /d builtin /f >/dev/null 2>&1

    say "Installing d3dx9 via winetricks (this may take a minute)..."
    winetricks -q d3dx9 >/dev/null 2>&1 || warn "winetricks d3dx9 returned non-zero; continuing."

    touch "$SETUP_MARKER"
fi

# 4. Build proxy DLL and helpers
say "Building proxy DLL and helpers..."
( cd d3d9_proxy && ./build.sh >/dev/null )

# 5. Deploy into Game Files/
if [ ! -d "Game Files" ]; then
    die "Game Files/ directory missing. Drop your Zoo Tycoon 2 files into a 'Game Files/' folder next to this script, then re-run."
fi

say "Deploying proxy into Game Files/..."
cp d3d9_proxy/d3d9.dll          "Game Files/"
cp d3d9_proxy/click_continue.exe "Game Files/"

# d3d9_real.dll: Wine's real d3d9, renamed so the proxy can load it.
WINE_PREFIX_DIR="$(wine --prefix 2>/dev/null || echo "$HOME/.wine")"
REAL_D3D9_CANDIDATES=(
    "/Applications/Wine Crossover.app/Contents/Resources/wine/lib/wine/i386-windows/d3d9.dll"
    "$WINE_PREFIX_DIR/lib/wine/i386-windows/d3d9.dll"
)
REAL_D3D9=""
for p in "${REAL_D3D9_CANDIDATES[@]}"; do
    if [ -f "$p" ]; then REAL_D3D9="$p"; break; fi
done
if [ -z "$REAL_D3D9" ]; then
    die "Could not locate Wine's built-in d3d9.dll. Tried: ${REAL_D3D9_CANDIDATES[*]}"
fi
cp "$REAL_D3D9" "Game Files/d3d9_real.dll"


# -- Cosmetic rebrand: make the Dock tooltip + menu bar read "Zoo Tycoon 2" --
say "Rebranding Wine Crossover's ntdll + wine-preloader for Zoo Tycoon 2..."
WINEDIR="/Applications/Wine Crossover.app/Contents/Resources/wine"
NTDLL="$WINEDIR/lib/wine/x86_64-unix/ntdll.so"
WP="$WINEDIR/bin/wine-preloader"
ZT_WP="$WINEDIR/bin/Zoo Tycoon 2"
# Back up originals once
[ ! -f "$NTDLL.bak_crossover" ] && cp "$NTDLL" "$NTDLL.bak_crossover"
[ ! -f "$WP.bak_crossover" ]    && cp "$WP"    "$WP.bak_crossover"
# Patch ntdll.so so it looks for "Zoo Tycoon 2" instead of "wine-preloader"
python3 -c '
p = "/Applications/Wine Crossover.app/Contents/Resources/wine/lib/wine/x86_64-unix/ntdll.so"
d = open(p, "rb").read()
old = b"wine-preloader\x00"
new = b"Zoo Tycoon 2\x00\x00\x00"
assert len(old) == len(new)
if old in d:
    d = d.replace(old, new)
    open(p, "wb").write(d)
    print("ntdll.so patched")
else:
    print("ntdll.so already patched")
'
# Create/refresh the renamed preloader with the new CFBundleName
cp "$WP" "$ZT_WP"
python3 -c '
p = "/Applications/Wine Crossover.app/Contents/Resources/wine/bin/Zoo Tycoon 2"
d = open(p, "rb").read()
old = b"CrossOver FOSS 23.7.1"
new = b"Zoo Tycoon 2         "
assert len(old) == len(new)
d = d.replace(old, new)
open(p, "wb").write(d)
print("Zoo Tycoon 2 binary patched (embedded CFBundleName)")
'
chmod +x "$ZT_WP"


say "Setup complete. Launch the game with:  ./play_zoo_tycoon.sh"