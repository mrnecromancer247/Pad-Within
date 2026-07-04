#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>   // LPUNKNOWN / IUnknown (not pulled by lean windows.h)

// ---------------------------------------------------------------------------
// Vtable-hook entry points (implemented in hook_di.cpp).
// ---------------------------------------------------------------------------

// Called right after the real DirectInput8Create succeeds. *ppvOut is the
// IDirectInput8* the game will use; we patch its vtable to intercept
// CreateDevice.
void Proxy_HookDirectInput8(void** ppvOut);

// ---------------------------------------------------------------------------
// SDL2 input backend (implemented in input_sdl.cpp).
// ---------------------------------------------------------------------------
void Proxy_InitInput();       // open SDL2, first available game controller
void Proxy_ShutdownInput();   // close SDL2
void Proxy_LogRawPad();       // DIAG: log currently-pressed SDL buttons/axes

// DIAG: patch the game exe's own IAT so we see every GetProcAddress() call it
// makes at runtime — reveals dynamic loading of APIs outside DirectInput
// (legacy joystick, XInput, RawInput) that our dinput8 hooks can't see.
void Proxy_HookGameIAT_GetProcAddress();

// Byte offsets (within the device's data buffer) of each standard axis, as
// reported by the device's EnumObjects. Some pads enumerate axes at NON-default
// offsets (DualSense swaps X/Y), so GetDeviceState must write each axis at its
// real offset rather than the fixed DIJOYSTATE field. -1 means "not present".
// Order: 0=X 1=Y 2=Z 3=Rx 4=Ry 5=Rz
struct AxisOffsets {
    int ofs[6];      // axis offsets
    int povOfs;      // first POV offset (-1 if none)
    int btnOfs[32];  // per-button-instance offset (-1 if absent)
};
extern AxisOffsets g_axisOfs;

// Forward-declared here so hook_di.cpp can call into the SDL backend.
// Fills a DIJOYSTATE (declared in <dinput.h>) from the current SDL pad state,
// applying our fixes (proper diagonal on X/Y, Z-axis triggers).
struct DIJOYSTATE;
void Proxy_FillJoyState(struct DIJOYSTATE* js);
