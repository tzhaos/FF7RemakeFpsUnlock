# FF7 Remake FPS Unlock

[中文](README.md)

## Overview

This tool removes the 120 FPS cap of *FINAL FANTASY VII REMAKE INTERGRADE*
(Unreal Engine 4.18) on PC.

It works as a proxy DLL that locates and patches the game's frame rate limiting
logic inside the game process. No game files are modified.

## Features

- Removes the 120 FPS cap; uncapped by default, with optional custom cap
- Signature-based scanning, no hardcoded addresses
- Does not modify game files; delete the DLL to revert
- Gamepads keep working; all XInput calls are forwarded to the system DLL
- Writes a runtime log for diagnostics
- Supports the Steam and Epic versions

## Requirements

- Windows 10 / 11 64-bit
- Game build: verified with Steam build 21888968 (2026-10-15)

## Installation

1. Copy `xinput1_3.dll` and, optionally, `FF7RFpsUnlock.ini` into:

   ```
   ...\FINAL FANTASY VII REMAKE\End\Binaries\Win64\
   ```

   next to `ff7remake_.exe`.
2. Launch the game, set **Framerate** to **120** in the graphics settings, and
   disable VSync.
3. The patch is applied automatically at process start. The result is written to
   `FF7RFpsUnlock.log` in the same directory.

## Configuration

The configuration file is `FF7RFpsUnlock.ini`, placed next to the DLL. It is
optional and uses the defaults when absent:

```ini
[Settings]
; 0 = uncapped (default); a positive value sets the FPS cap, e.g. 144
FPS=0
; Disable UE4's smoothed frame rate clamp (used above 120)
DisableSmoothing=1
```

The in-game options remain 30 / 60 / 90 / 120; this tool acts on the "120" option.

## Log

`FF7RFpsUnlock.log` records the module base address, signature search results and
patch status. If a game update removes a signature, the log reports it and the game
is left unmodified.

## How it works

When the "120" framerate option is applied, the game writes two console variables:

- `t.MaxFPS`: the game-thread frame limiter
- `RHI.TargetRefreshRate`: the D3D11 RHI present-rate limiter

UE4.18 may additionally clamp the frame rate through `bSmoothFrameRate` /
`SmoothedFrameRateRange`.

The tool scans the `.text` section for the code block that applies this option,
verifies that the referenced constant is `120.0`, then changes the limit to 0
(uncapped) or a custom value (through a trampoline allocated near the patch site).
It also clears `RHI.TargetRefreshRate` and, when configured, skips the smoothed
frame rate clamp.

`xinput1_3.dll` is the XInput 1.3 gamepad runtime shipped with Windows. The game
executable statically imports this DLL, and Windows searches the executable's
directory first when loading a DLL. The tool's DLL enters the game process through
this mechanism and forwards all XInput exports to
`C:\Windows\System32\xinput1_3.dll`.

## Build

Requirements: Visual Studio (Desktop development with C++ workload) and CMake 3.20+.

```bat
build.bat
```

or:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output: `build\Release\xinput1_3.dll`

## Uninstall

Delete `xinput1_3.dll` and `FF7RFpsUnlock.ini` from the game's `Win64` directory.

## License

MIT. See [LICENSE](LICENSE).

Not affiliated with Square Enix. FINAL FANTASY is a trademark of Square Enix.
