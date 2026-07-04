# Pad-Within

A universal SDL2 gamepad proxy for **Prince of Persia: Warrior Within** (2005, GOG/Steam PC).

Works with **any SDL2-supported controller** — DualSense, DualShock 4, Xbox pads, 8BitDo, and more — not just XInput devices. Fixes the game's broken native gamepad handling: the shared trigger axis, dead diagonals, and a nasty axis-binding bug that makes the Controls menu unable to tell your stick's vertical and horizontal axes apart.

## Features

- **Works with any pad SDL2 supports**, no Xidi/DS4Windows/XInput translation layer required.
- **Independent triggers** — L2/R2 no longer cancel each other out when both are read from the same axis.
- **Clean diagonal movement** — proper radial deadzone instead of the game's coarse per-axis clipping.
- **Fixes axis mis-binding** — the in-game Controls menu can finally tell vertical and horizontal stick movement apart (see [Technical Notes](#technical-notes) for why this was broken).
- **Fully configurable** via a plain-text ini: sensitivity, deadzone, stick range calibration for worn sticks, button remapping, axis inversion, and more — no recompiling needed.
- **GOG and Steam compatible** (GOG's exe needs one extra one-time step, see below — both builds share identical code, confirmed byte-for-byte identical `.text`).

## Installation

1. Download the latest **Pad-Within** release from GitHub.
2. Extract the files into the folder where the game's exe (`POP2.exe` / `pop2.exe`) lives.
   - **GOG version only:** the GOG exe ships UPX-compressed, which prevents the proxy from loading. Run `tools\upx.bat` once from that folder first (it unpacks `pop2.exe` in place using the included `tools\upx.exe`). This is safe and one-time; back up the exe first if you want extra peace of mind. The Steam exe doesn't need this step.
3. Turn off **Steam Input** for this game (Steam Input remaps your controller before the game ever sees it, which interferes with this proxy).
4. Plug in your controller and launch the game. Open the in-game **Controls → Gamepad** settings and bind your controller using the layout below.

### Control layout

| Action | Binding |
|---|---|
| Move | Left stick — **X, Y** axes |
| Camera | Right stick — **RX, RY** axes |
| Roll / Jump / Eject / Accept | **A** / Cross |
| Pickup / Throw / Climb down / Cancel | **B** / Circle |
| Sword Attack | **X** / Square |
| Grab / 2nd weapon attack | **Y** / Triangle |
| Rewind / Slowdown | **LB** / L1 |
| Walling / Block | **RB** / R1 |
| Camera Look | **RT** / R2 |
| Alternate View | **LT** / L2 |
| Reset Camera | **RS** / R3 |
| Start / Pause | **Start** |
| Navigation Map | **Back** / Select |
| Menu navigation | **D-pad** |

Bindings are saved by the game itself (`Profile.DAT`), so you only need to do this once per controller — unless you enable `SpoofVidPid` in the ini (see below), in which case it only needs doing once ever, regardless of which controller you plug in later.

## Configuration

All settings live in `PadWithin.ini`, next to `dinput8.dll`. The file is heavily commented — open it in any text editor. No ini present = defaults are used. Highlights:

- **`[Sensitivity]`** — separate deadzone for movement and camera, camera turn speed (as a percentage, 65 default), and `MoveMaxStickRange` / `CameraMaxStickRange` for calibrating a worn stick that never reaches full physical deflection.
- **`[Axes]`** — invert any axis, swap which trigger drives which direction.
- **`[Buttons]`** — remap any face/shoulder/stick-click button to a different in-game slot.
- **`[Spoof]`** — optionally present a fixed controller identity to the game so your bindings persist even when switching between different physical pads.
- **`[General]`** — `EnableLog=1` turns on `PadWithin.log` for troubleshooting; leave off otherwise.

## Building from source

Requires: MSVC (x86 target — the game is 32-bit), and the [SDL2](https://github.com/libsdl-org/SDL/releases) development libraries (the `-devel-...-VC.zip` package).

From an **x86 Native Tools Command Prompt for VS**, in the project folder:

```bat
cl /nologo /LD /std:c++17 /EHsc /O2 /MD /I include /I "<path-to-SDL2>\include" ^
   src\dllmain.cpp src\hook_di.cpp src\input_sdl.cpp src\config.cpp src\log.cpp ^
   /link /DEF:dinput8.def /OUT:dinput8.dll ^
   "<path-to-SDL2>\lib\x86\SDL2.lib" dxguid.lib user32.lib
```

Copy the resulting `dinput8.dll`, plus `SDL2.dll` from the SDL2 package's `lib\x86` folder, next to the game's exe.

`src/iat_hook.cpp` is a leftover diagnostic tool (see Technical Notes) and is not part of the normal build.

## Known issues

- The in-game "Gamepad Sensitivity" slider (Controls menu) doesn't reliably do anything with this proxy — use `CameraSensitivity` in the ini instead.
- The Navigation Map screen can hang/fail to display for some players. This is a pre-existing base-game/PC-port issue independent of this mod (reproducible with keyboard input too) — not something this proxy causes or can fix.
- Coexisting with a separately-installed widescreen fix requires chaining two `dinput8.dll`-based mods; see the comments in `src/dllmain.cpp` (`ChainAsiLoader`) if you need this — rename the widescreen mod's `dinput8.dll` to `d8hooked.dll` and this proxy will load it automatically.

## Technical Notes

For anyone extending this or debugging a similar game, a summary of what actually made Warrior Within's gamepad support this broken:

1. **Shared Z-axis for triggers.** Like many mid-2000s DirectInput games, WW reads L2/R2 off a single physical Z-axis, so pressing both simultaneously cancels out. This proxy reads them from SDL as two independent trigger axes.
2. **The real bug: axis-binding detection, not axis data.** The Controls menu's "which axis moved" detection appears to grab the *first* axis that crosses a tiny threshold while scanning in a fixed order, rather than the *largest* deflection. Because no physical stick moves on a perfectly pure axis (a "pure" vertical push always leaks a hair of horizontal), that tiny cross-axis noise on X consistently wins the race before Y is ever seen — even though actual gameplay (once a binding exists) reads both axes correctly. The fix (`AxisSnapRatio` in the ini) snaps the minor axis to exactly zero during a dominant push, so the detector can only see the axis you're actually moving.
3. **Investigated and ruled out along the way:** device enumeration offsets, DirectInput data-format layout, multiple simultaneously-created joystick devices, `Gamepads.DAT` VID/PID-based axis remapping (including spoofing as a known-good Xbox 360 identity), buffered vs. immediate input mode, `Poll`/`Acquire`/`SetEventNotification`, and a cached device-identity blob inside the game's own `Profile.DAT`. None of these were the actual cause — all preserved as commented-out/inactive hooks in `hook_di.cpp` for reference.
4. **GOG vs. Steam.** Both ship the identical game binary — confirmed byte-identical `.text`/`.data` sections — but GOG's copy is UPX-compressed, which changes how Windows resolves `dinput8.dll` at load time and prevents this proxy from being picked up. Unpacking with UPX (`tools/upx.bat`) makes it byte-for-byte match the Steam binary's loader behavior.

## Credits

Built with [SDL2](https://www.libsdl.org/). Thanks to the Prince of Persia PC modding community (ThirteenAG, Xidi) for prior art on DirectInput proxying for this game.
