# Zoo Tycoon 2 Ultimate Collection on macOS

Play Zoo Tycoon 2 Ultimate Collection on your Mac — including Apple Silicon Macs (M1, M2, M3, M4).

The game runs in a resizable window with 4:3 aspect ratio. Drag the window edges to make it as big or small as you want — the game scales to fit. All expansion packs included.

## Quick start

You'll need [Homebrew](https://brew.sh) installed. Open Terminal and run:

```bash
# Install Wine (runs Windows games on Mac)
brew install --cask wine-crossover
brew install mingw-w64
brew install winetricks

# One-time Wine setup
wine reg add "HKCU\\Software\\Wine\\Direct3D" /v renderer /t REG_SZ /d gl /f
wine reg add "HKCU\\Software\\Wine\\Direct3D" /v VideoMemorySize /t REG_SZ /d 512 /f
wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v d3d9 /t REG_SZ /d native /f
wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v d3d9_real /t REG_SZ /d builtin /f
winetricks -q d3dx9

# Build the compatibility layer
cd d3d9_proxy
./build.sh

# Deploy it
cp d3d9.dll click_continue.exe "../Game Files/"
cp "$(wine --prefix)/lib/wine/i386-windows/d3d9.dll" "../Game Files/d3d9_real.dll"
```

Then put your Zoo Tycoon 2 game files in the `Game Files/` folder and run:

```bash
./play.sh
```

## What you get

- Runs natively on Apple Silicon (M1/M2/M3/M4) and Intel Macs
- Resizable window — drag to any size, game scales to fill
- 4:3 aspect ratio locked so nothing stretches weird
- All intro videos, menus, and gameplay work
- All DLC and expansion packs supported
- No need to mess with Wine settings — the launch script handles everything

## Requirements

- macOS on Apple Silicon or Intel (tested on macOS Tahoe, M1 Max)
- Your own copy of Zoo Tycoon 2 Ultimate Collection
- ~2 GB disk space

## License

Provided as-is for personal use. You need your own copy of Zoo Tycoon 2. Game files are not included.

---

# Technical details

Everything below is for developers or anyone curious about how this works.

## How it works

Zoo Tycoon 2 is a 2004 Windows DirectX 9 game that checks for specific NVIDIA/ATI graphics cards at startup and refuses to run if it doesn't find one. Even Wine can't run it out of the box.

A custom proxy DLL (`d3d9.dll`) sits between the game and Wine's D3D9 implementation. It spoofs the GPU identity, patches error checks in the game binary at runtime (after SafeDisc decryption), injects display modes that Retina displays don't natively report, and forces windowed mode.

### Proxy DLL details

The proxy:

- Spoofs GPU as NVIDIA GeForce 7900 GTX (vendor 0x10DE, device 0x0290)
- Injects standard display modes (640x480, 800x600, etc.) for Retina compatibility
- Forces windowed mode (macOS can't do 640x480 fullscreen on Retina)
- Swaps D3DFMT_D32 depth buffer for D3DFMT_D24S8 (Wine's OpenGL backend doesn't support D32)
- Hooks ChangeDisplaySettings and MessageBoxA via IAT patching
- Runs EarlyPatchThread after SafeDisc decompression to patch error checks
- Runtime binary patches after SafeDisc decryption:
  - capCheck at 005BBC67: JGE -> JMP
  - vidCheck at 007F1AF6: E_FAIL -> S_OK, mode count check NOPed
  - Renderer creation gate at 005BBCD1: forced to pass
- Patches wined3d.dll framebuffer completeness checks (macOS OpenGL FBO quirks)

### Window scaling

The game renders at 640x480 internally. A patch to Wine's `winemac.drv` handles scaling:

- WineContentView pinned to 640x480 (GL framebuffer stays at game resolution)
- CATransform3D on the view's layer scales content to fill the window
- contentsGravity=kCAGravityResize for 2D surface content (intro videos)
- windowWillResize enforces 4:3 aspect ratio with 640x480 minimum
- setFrame:display: blocks Wine's programmatic shrinks
- Mouse coordinates scaled from window space to 640x480 game space in cocoa_app.m
- macdrv_get_cursor_position scaled so hover effects work when cursor is stationary
- NSTrackingArea expanded to cover full window content area

The patch is at `d3d9_proxy/winemac_complete.patch`.

### Rendering path

```
ZT.exe
 -> d3d9.dll (proxy: GPU spoof, mode injection, binary patches)
   -> d3d9_real.dll (Wine's builtin d3d9)
     -> wined3d.dll (D3D9-to-OpenGL translation)
       -> macOS OpenGL
```

### Why this is hard

The game uses the Gamebryo (NetImmerse) engine with a `NiDX9Renderer` class that validates GPU capabilities at startup. It calls GetAdapterIdentifier, GetDeviceCaps, and EnumAdapterModes, then checks results against an internal format table. Anything unexpected = refused to start.

The executable is SafeDisc protected (encrypted code sections `stxt774`, `stxt371`). Binary patches must be applied after runtime decryption from inside a loaded DLL.

macOS adds more problems: no 640x480 fullscreen on Retina, deprecated OpenGL with broken framebuffer operations, and Wine's ChangeDisplaySettings failing for non-native resolutions.

### Files

- `d3d9_proxy/d3d9_proxy.c` — proxy DLL (GPU spoof, mode injection, binary patches, FBO fixes)
- `d3d9_proxy/cds_hook.c` — ChangeDisplaySettings IAT hook
- `d3d9_proxy/click_continue.c` — dialog auto-clicker
- `d3d9_proxy/winemac_complete.patch` — Wine winemac.drv patch for window scaling + mouse mapping
- `play.sh` — launch script
