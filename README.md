# Zoo Tycoon 2 Ultimate Collection on macOS (Apple Silicon)

Zoo Tycoon 2 Ultimate Collection (2004) is a Windows-only DirectX 9 game. It doesn't run on macOS out of the box. Even [Wine](https://www.winehq.org/) (a compatibility layer that runs Windows software on other operating systems) can't handle it on its own, because the game checks for specific NVIDIA/ATI graphics cards from 2004 and refuses to start if it doesn't find one.

This repo contains a custom proxy DLL and runtime binary patches that trick the game into running. It plays in a resizable macOS window through Wine's OpenGL backend, with the game content scaling to fill whatever window size you choose.

## What the proxy does

The proxy DLL (`d3d9.dll`) loads between the game and Wine's real D3D9 implementation. It:

- Tells the game the GPU is an NVIDIA GeForce 7900 GTX (the game checks vendor/device IDs at startup and refuses to run otherwise)
- Injects standard display modes (640x480, 800x600, 1024x768) because Retina displays only report scaled resolutions like 960x600, which the game's Gamebryo engine doesn't recognize
- Forces windowed mode instead of fullscreen, since macOS won't switch to 640x480 and Wine fullscreen traps your keyboard
- Swaps the game's D3DFMT_D32 depth buffer request for D3DFMT_D24S8, which Wine's OpenGL backend actually supports
- Hooks `ChangeDisplaySettings` via IAT patching so the game thinks display mode changes succeed
- Hooks `MessageBoxA` to suppress error dialogs, with an auto-dismiss thread for the game's custom error windows
- Runs an `EarlyPatchThread` that waits for SafeDisc to decompress the code, then patches error checks before the game's init can trigger them
- Patches the game binary at runtime after SafeDisc decryption:
  - `capCheck` at 005BBC67: forced to pass (JGE -> JMP)
  - `vidCheck` at 007F1AF6: E_FAIL return patched to S_OK, internal mode count check NOPed
  - Renderer creation gate at 005BBCD1: forced to pass
- Patches wined3d.dll's framebuffer completeness checks (macOS OpenGL returns GL_INVALID_FRAMEBUFFER_OPERATION on certain FBO configurations that work fine on Linux)

There's also `click_continue.exe`, a small Win32 app that runs alongside the game and automatically clicks through the EULA and any remaining warning dialogs.

## Window scaling

The game renders internally at 640x480, but the window is freely resizable. A custom patch to Wine's `winemac.drv` handles the scaling:

- The WineContentView is pinned to 640x480 (keeping the GL framebuffer at game resolution)
- A `CATransform3D` on the view's layer visually scales the content to fill the window
- `contentsGravity = kCAGravityResize` handles 2D surface content (intro videos)
- `windowWillResize:toSize:` enforces 4:3 aspect ratio with a 640x480 minimum
- `setFrame:display:` blocks Wine's programmatic shrinks back to 640x480
- Mouse coordinates are scaled from window space to 640x480 game space in `cocoa_app.m` and `macdrv_get_cursor_position`
- The NSTrackingArea is expanded to cover the full window so hover events work everywhere

The patch is saved as `d3d9_proxy/winemac_complete.patch` and must be applied to Wine's source tree before building `winemac.so`.

## Requirements

- macOS on Apple Silicon (tested on Tahoe 26.4, M1 Max)
- [Wine Crossover](https://github.com/nicehash/wine) (a free compatibility layer that lets you run Windows apps on macOS)
- mingw cross-compiler for building (`brew install mingw-w64`)
- Your own copy of Zoo Tycoon 2 Ultimate Collection

## Setup

```bash
# Install dependencies
brew install --cask wine-crossover
brew install mingw-w64
brew install winetricks

# Configure Wine
wine reg add "HKCU\\Software\\Wine\\Direct3D" /v renderer /t REG_SZ /d gl /f
wine reg add "HKCU\\Software\\Wine\\Direct3D" /v VideoMemorySize /t REG_SZ /d 512 /f
wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v d3d9 /t REG_SZ /d native /f
wine reg add "HKCU\\Software\\Wine\\DllOverrides" /v d3d9_real /t REG_SZ /d builtin /f
wine reg add "HKCU\\Software\\Microsoft\\Microsoft Games\\Zoo Tycoon 2" /v FIRSTRUN /t REG_DWORD /d 1 /f
wine reg add "HKCU\\Software\\Microsoft\\Microsoft Games\\Zoo Tycoon 2" /v EULAAccepted /t REG_DWORD /d 1 /f

# Install the D3DX9 runtime (needed for shader/effect compilation)
winetricks -q d3dx9

# Build
cd d3d9_proxy
./build.sh

# Deploy
cp d3d9.dll click_continue.exe "../Game Files/"
cp "$(wine --prefix)/lib/wine/i386-windows/d3d9.dll" "../Game Files/d3d9_real.dll"

# Play
cd ..
./play_zoo_tycoon.sh
```

## Rendering path

```
ZT.exe
 -> d3d9.dll (proxy: GPU spoof, mode injection, binary patches)
   -> d3d9_real.dll (Wine's builtin d3d9)
     -> wined3d.dll (D3D9-to-OpenGL translation)
       -> macOS OpenGL
```

The Vulkan path (wined3d -> MoltenVK -> Metal) also creates a working device and swapchain, but the game doesn't actually render through it. OpenGL works.

## Why this is hard

The game uses the Gamebryo (NetImmerse) engine, which has a `NiDX9Renderer` class that runs a long validation routine at startup (`vidCheck` at 0x7F1AF6). This function calls `GetAdapterIdentifier`, `GetDeviceCaps`, and `EnumAdapterModes`, then runs the results through an internal format table and mode validation loop. If anything doesn't match what Gamebryo expects from a circa-2004 NVIDIA or ATI card, it bails.

On top of that, the game executable is protected with SafeDisc, which encrypts code sections (`stxt774`, `stxt371`) and only decrypts them at runtime. Binary patches have to be applied after decryption happens, which means doing it from inside a DLL that the game loads (our d3d9 proxy).

macOS adds its own problems: no 640x480 fullscreen mode on Retina displays, deprecated OpenGL with broken framebuffer operations, and Wine's `ChangeDisplaySettings` returning DISP_CHANGE_BADMODE for any resolution the display doesn't natively support.

## Files

- `d3d9_proxy/d3d9_proxy.c` - the proxy DLL (GPU spoof, mode injection, binary patches, FBO fixes)
- `d3d9_proxy/cds_hook.c` - ChangeDisplaySettings IAT hook
- `d3d9_proxy/click_continue.c` - dialog auto-clicker
- `d3d9_proxy/winemac_complete.patch` - Wine winemac.drv patch for window scaling + mouse mapping
- `play.sh` - launch script

## License

Provided as-is for personal use. You need your own copy of Zoo Tycoon 2. Game files are not included.
