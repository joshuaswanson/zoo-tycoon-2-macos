# Zoo Tycoon 2 Ultimate Collection on macOS

Play Zoo Tycoon 2 Ultimate Collection on your Mac, including Apple Silicon Macs (M1, M2, M3, M4).

The game runs in a resizable window with 4:3 aspect ratio. Drag the window edges to make it as big or small as you want and the game scales to fit. Click the green fullscreen button to play fullscreen. The game is letterboxed to preserve 4:3.

## Quick start

You'll need [Homebrew](https://brew.sh) installed and the game files. The repo does not ship the game — grab a copy from [oldgamesdownload.com](https://oldgamesdownload.com/zoo-tycoon-2-ultimate-collection/) (or use your own disc), extract it, and put the contents into a `Game Files/` folder next to the scripts. Then in Terminal run:

```bash
./setup.sh
./play_zoo_tycoon.sh
```

`setup.sh` installs Wine Crossover, mingw-w64, and winetricks, configures Wine, builds the compatibility layer, and deploys everything into `Game Files/`. It is idempotent, so re-running it after updates is safe.

## What you get

- Runs natively on Apple Silicon (M1/M2/M3/M4) and Intel Macs
- Resizable window that scales the game to fill
- macOS-native fullscreen via the green button (letterboxed 4:3)
- 4:3 aspect ratio locked so nothing stretches weird
- Mouse cursor, click, and hover all stay aligned with visuals in windowed, fullscreen, and after moving/resizing the window
- All intro videos, menus, and gameplay work
- Works with all DLC and expansion packs
- No need to mess with Wine settings, the launch script handles everything

## Known issues

- Mouse-look mode (holding right-click and dragging to rotate the camera) has a slight upward drift when the window is larger than 640x480. Works fine at default window size.

## Requirements

- macOS on Apple Silicon or Intel (tested on macOS Tahoe, M1 Max)
- Your own copy of Zoo Tycoon 2 Ultimate Collection (not included in this repo; available at [oldgamesdownload.com](https://oldgamesdownload.com/zoo-tycoon-2-ultimate-collection/) or from your own disc)
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
- Forces D3D9 windowed mode (macOS handles fullscreen at the AppKit layer, letterboxing the 640x480 framebuffer)
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
- Uniform scaling with centering to maintain aspect ratio (4:3 letterboxed into 16:9 fullscreen)
- contentsGravity=kCAGravityResize for 2D surface content (intro videos)
- windowWillResize enforces 4:3 aspect ratio with 640x480 minimum
- setFrame:display: blocks Wine's programmatic shrinks
- windowDidEnterFullScreen re-runs windowDidResize so the layer transform gets re-applied after AppKit's fullscreen transition
- `cocoa_window.m` publishes `g_real_window_*` globals (current on-screen window rect) because Wine's internal `wineFrame` goes stale when the user drags the window or enters macOS fullscreen
- Mouse coordinate scaling uses `g_real_window_*` for the scaling reference and `NtUserClientToScreen` for the output base, so the game's `ScreenToClient` yields correct 640x480 client coords regardless of Wine's stale state; runs at all window sizes so moving the default-size window also works
- macdrv_get_cursor_position (GetCursorPos polling) mirrors the same transform so hover effects persist when the cursor is stationary
- macdrv_SetCursorPos inverse-scales the requested cursor position so mouse-look games that re-center the cursor each frame warp the hardware cursor to the actual visual center
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

The game uses the Gamebryo (NetImmerse) engine with a `NiDX9Renderer` class that validates GPU capabilities at startup. It calls GetAdapterIdentifier, GetDeviceCaps, and EnumAdapterModes, then checks results against an internal format table. Anything unexpected and it refuses to start.

The executable is SafeDisc protected (encrypted code sections `stxt774`, `stxt371`). Binary patches must be applied after runtime decryption from inside a loaded DLL.

macOS adds more problems: no 640x480 fullscreen via D3D on Retina (solved by forcing windowed internally and letterboxing at the AppKit layer), deprecated OpenGL with broken framebuffer operations, and Wine's ChangeDisplaySettings failing for non-native resolutions. AppKit-level fullscreen also doesn't update Wine's internal window rect, requiring a separate path that reads the real on-screen window position from the Cocoa side.

### Files

- `d3d9_proxy/d3d9_proxy.c` — proxy DLL (GPU spoof, mode injection, binary patches, FBO fixes)
- `d3d9_proxy/cds_hook.c` — ChangeDisplaySettings IAT hook
- `d3d9_proxy/click_continue.c` — dialog auto-clicker
- `d3d9_proxy/winemac_complete.patch` — Wine winemac.drv patch for window scaling + mouse mapping (touches `cocoa_window.m`, `cocoa_window.h`, `cocoa_app.m`, `cocoa_opengl.m`, `mouse.c`)
- `setup.sh` — one-shot installer (deps, Wine config, proxy build, deploy)
- `play_zoo_tycoon.sh` — launch script
