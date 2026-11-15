# Preview
<p align="center">
  <img src="Guppy Internal/Images/4Github2.png" width="600" alt="Console menu">
</p>

<p align="center">
  <img src="Guppy Internal/Images/4Github1.png" width="600" alt="Console menu">
</p>

<p align="center">
  <img src="Guppy Internal/Images/GuppyMenu.png" width="600" alt="Console menu">
</p>

# CS2 Internal

A fully-featured internal cheat for Counter-Strike 2, written in C++ and rendered
through a DirectX 11 + ImGui overlay. The overlay is a layered, click-through,
topmost window that automatically tracks the CS2 client area, so visuals sit
exactly over the game with no manual positioning.

## Features

### ESP (Extra Sensory Perception)
- 2D box ESP sized to player height
- Health bar with red→green gradient
- Player name (cached, no per-frame RPM)
- Distance in meters
- Full skeleton (22 bones, drawn from cached bone array)
- Head dot indicator
- Optional team check to skip teammates

### Aimbot
- Adjustable FOV (visualized as a circle on screen)
- Adjustable smoothing (1.0 = instant, 15.0 = very slow)
- Hold **RMB** to activate
- Targets nearest player to crosshair within FOV
- Uses cached bone positions for aim point

### Triggerbot
- Auto-fires when crosshair is on an enemy
- Respects team check
- Toggle via F3

### Bunnyhop
- Hold **SPACE** to auto-bhop
- Toggle via F5

### Live Console Menu
- Renders a real-time ASCII menu in the attached console
- Shows current ON/OFF state of every feature
- Shows live FOV and smoothing values as you adjust them
- Redraws in place (no scroll/flicker)

## Technical details

- **Overlay:** `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`
  — click-through, never steals focus, sits above the game.
- **Window tracking:** overlay repositions only when the game window
  moves/resizes (avoids waking DWM every frame).
- **Caching:** entity list, controllers, pawns, names, teams, and bone array
  pointers are cached and refreshed every 3s (or on demand). Bone positions are
  batch-read in a single `ReadProcessMemory` call per player instead of 22
  separate reads.
- **Pruning:** caches are pruned of dead pawns each refresh to prevent stale
  pointers.
- **World-to-screen:** standard view-matrix projection with screen-bounds
  culling before drawing.
- **Graceful teardown:** on F11 or DLL unload, ImGui, D3D11, the overlay window,
  the window class, and the console are all released in order.

## Controls

| Key        | Action                          |
|------------|---------------------------------|
| F1         | ESP on/off                      |
| F2         | Team check on/off               |
| F3         | Triggerbot on/off               |
| F4         | Aimbot on/off                   |
| F5         | Bunnyhop on/off                 |
| `[`        | Decrease FOV                    |
| `]`        | Increase FOV                    |
| `-`        | Decrease aimbot smoothing       |
| `+`        | Increase aimbot smoothing       |
| **RMB**    | Hold to aimbot                  |
| **LMB**    | Fire / shoot                    |
| **SPACE**  | Hold to bunnyhop                |
| F11        | Exit cheat                      |

## Build

- Visual Studio 2022 (x64)
- C++17 or later
- DirectX 11 SDK (bundled with Windows SDK)
- ImGui (DX11 + Win32 backends)
- Linked against `d3d11.lib`, `dxgi.lib`

## Notes

- Offsets live in `offsets.h` / `client_dll.hpp` /  `buttons.h` and must be updated after
  CS2 patches.
- The overlay uses a color-keyed layered window; the render target is cleared
  to `(0,0,0,0)` so only ImGui draw data is visible.
- Console menu is ASCII-only so it renders correctly regardless of code page.

> For educational and research purposes only.

## Installation

### Prerequisites

- **Visual Studio 2022** with the *Desktop development with C++* workload
- **vcpkg** cloned and bootstrapped (`.\vcpkg\bootstrap-vcpkg.bat`)
- **Extreme Injector v3.7.2** downloaded and extracted

---

### 1. Install ImGui via vcpkg

From your vcpkg directory, run:
`vcpkg install imgui[win32-binding,dx11-binding]:x64-windows
vcpkg integrate install`

This pulls the correct ImGui build with the Win32 and DX11 bindings and wires the
include/link paths into Visual Studio. After integrate install, you can simply
#include "imgui.h" with no manual linker setup.

2. Build the DLL

  Set the configuration to Release and platform to x64.

  Link against the DirectX libraries: d3d11.lib, dxgi.lib.

  Build the project to produce Guppy Internal.dll.

3. Inject with Extreme Injector

    Manual Map is required. Standard LoadLibrary injection will fail or be
    detected because the DLL manages its own dependencies and initialization.

4. Launch CS2 and wait until you reach the main menu.

5. Run Extreme Injector v3.7.2 as Administrator.

6. Open Settings and set Injection Method -> Manual Map.

7. Click OK to save. Click select and choose cs2.exe process, Add DLL and select Guppy Internal.dll

8.  lick Inject.

  A console window should appear immediately with the live menu.
  
  Press F11 to unload the cheat.



