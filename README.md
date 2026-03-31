# Zoo Tycoon 2 Ultimate Collection on macOS (Apple Silicon)

Zoo Tycoon 2 Ultimate Collection (2004) is a 32-bit Windows DirectX 9 game. It does not run on macOS out of the box, and Wine alone can't handle it either -the game has a GPU vendor whitelist, SafeDisc copy protection, and a startup validation routine that rejects anything that isn't a real NVIDIA or ATI card from 2004.

This repo contains a custom `d3d9.dll` proxy and a set of runtime binary patches that fix all of that. The game runs in a normal macOS window through Wine's OpenGL backend.

## What the proxy does

The proxy DLL (`d3d9.dll`) loads between the game and Wine's real D3D9 implementation. It:

- Tells the game the GPU is an NVIDIA GeForce 7900 GTX (the game checks vendor/device IDs at startup and refuses to run otherwise)
- Injects standard display modes (640x480, 800x600, 1024x768) because Retina displays only report scaled resolutions like 960x600, which the game's Gamebryo engine doesn't recognize
- Forces windowed mode instead of fullscreen, since macOS won't switch to 640x480 and Wine fullscreen traps your keyboard
- Swaps the game's D3DFMT_D32 depth buffer request for D3DFMT_D24S8, which Wine's OpenGL backend actually supports
- Hooks `ChangeDisplaySettings` via IAT patching so the game thinks display mode changes succeed
- Patches the game binary at runtime after SafeDisc decryption:
  - `capCheck` at 005BBC67: forced to pass (JGE -> JMP)
  - `vidCheck` at 007F1AF6: E_FAIL return patched to S_OK, internal mode count check NOPed
  - Renderer creation gate at 005BBCD1: forced to pass
- Patches wined3d.dll's framebuffer completeness checks (macOS OpenGL returns GL_INVALID_FRAMEBUFFER_OPERATION on certain FBO configurations that work fine on Linux)

There's also `click_continue.exe`, a small Win32 app that runs alongside the game and automatically clicks through the EULA, the "old driver" warning dialog, and the "unable to create renderer" error dialog. The error dialog's Continue button is normally grayed out -the tool force-enables it and clicks Safe Mode.

## Requirements

- macOS on Apple Silicon (tested on Tahoe 26.4, M1 Max)
- Wine Crossover (free -`brew install --cask wine-crossover` or equivalent)
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
 -> d3d9.dll (this proxy -spoofs GPU, injects modes, patches binary)
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

- `d3d9_proxy/d3d9_proxy.c` -the proxy DLL (GPU spoof, mode injection, binary patches, FBO fixes)
- `d3d9_proxy/cds_hook.c` -ChangeDisplaySettings IAT hook
- `d3d9_proxy/click_continue.c` -dialog auto-clicker
- `d3d9_proxy/list_windows.c` -Wine window enumerator (debug tool)
- `play_zoo_tycoon.sh` -launch script

## License

Provided as-is for personal use. You need your own copy of Zoo Tycoon 2. Game files are not included.
