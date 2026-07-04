// =============================================================================
//  pop2_gamepad  -  dinput8.dll proxy for Prince of Persia: Warrior Within
//                   (GOG + Steam, identical .text @ TimeDateStamp 0x418bfd6f)
//
//  Goal: fix the DirectInput joystick handling that the game does badly:
//    - combined LT/RT on a single Z-axis  -> split into independent triggers
//    - diagonal movement broken by coarse deadzone on X/Y
//    - reliable controller detection without Steam Input
//
//  Strategy (proven pattern from "Dark Controllers of the Earth"):
//    1. We ARE dinput8.dll (only input DLL the exe imports; IAT @0x8bb014).
//    2. In DllMain we manually LoadLibrary the REAL system dinput8.dll and
//       forward DirectInput8Create there.
//    3. If the user runs the ThirteenAG widescreen ASI loader, it must be
//       renamed dinput8.dll -> d8hooked.dll; we LoadLibrary it ourselves so
//       its DllMain runs and loads the .asi plugins (widescreen works),
//       exactly like the Call of Cthulhu chain. Names loaded via LoadLibrary
//       are NOT bound by the exe import table, so any name works.
//    4. On the returned IDirectInput8, hook CreateDevice; on each device,
//       hook GetDeviceState/GetDeviceData and synthesize DIJOYSTATE2 from
//       SDL2. See input_sdl.cpp for the actual fill logic (stubbed for now).
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

#include "proxy.h"
#include "log.h"
#include "config.h"

// Our own module handle (for locating the ini next to the DLL).
HMODULE g_selfModule = nullptr;

// Real system dinput8.dll and its DirectInput8Create.
static HMODULE g_realDInput = nullptr;
static HMODULE g_asiChain   = nullptr;   // d8hooked.dll (renamed ASI loader), optional
using DirectInput8Create_t = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8Create_t g_realDI8Create = nullptr;

// ---------------------------------------------------------------------------
// Load the genuine dinput8.dll from the system directory (never from the game
// folder, or we'd load ourselves recursively).
// ---------------------------------------------------------------------------
static bool LoadRealDInput()
{
    char sysdir[MAX_PATH];
    if (!GetSystemDirectoryA(sysdir, MAX_PATH)) return false;
    std::string path = std::string(sysdir) + "\\dinput8.dll";

    g_realDInput = LoadLibraryA(path.c_str());
    if (!g_realDInput) {
        LOG("FATAL: could not load real dinput8 at %s (err %lu)", path.c_str(), GetLastError());
        return false;
    }
    g_realDI8Create = reinterpret_cast<DirectInput8Create_t>(
        GetProcAddress(g_realDInput, "DirectInput8Create"));
    if (!g_realDI8Create) {
        LOG("FATAL: real dinput8 has no DirectInput8Create export");
        return false;
    }
    LOG("real dinput8 loaded @ %p, DI8Create @ %p", (void*)g_realDInput, (void*)g_realDI8Create);
    return true;
}

// ---------------------------------------------------------------------------
// Optionally chain the widescreen ASI loader. We look for d8hooked.dll next to
// the exe. If present, loading it runs its DllMain which pulls in the .asi
// plugins. We deliberately do NOT forward DirectInput8Create through it — we go
// straight to the system DLL — so our behaviour is independent of what that
// third-party stub exports or how it changes in future versions.
// ---------------------------------------------------------------------------
static void ChainAsiLoader()
{
    g_asiChain = LoadLibraryA("d8hooked.dll");
    if (g_asiChain)
        LOG("chained ASI loader d8hooked.dll @ %p (widescreen etc.)", (void*)g_asiChain);
    else
        LOG("no d8hooked.dll found (err %lu) - running without ASI chain, that's fine",
            GetLastError());
}

// ---------------------------------------------------------------------------
// Exported entry point the game calls. Signature must match dinput8.h exactly.
//
// All heavy initialization happens HERE, on first call, NOT in DllMain.
// DllMain runs under the Windows loader lock, where calling LoadLibrary or
// starting threads (as SDL_Init does) can deadlock the process before the
// game ever creates its window. DirectInput8Create is called by the game
// after startup, outside the loader lock, so it is safe here.
// ---------------------------------------------------------------------------
static bool g_initDone = false;

static void EnsureInit()
{
    if (g_initDone) return;
    g_initDone = true;                 // set first: never retry a failed init
    Config_Load("PadWithin.ini");      // load tunables (defaults if absent)
    if (g_cfg.enableLog)               // open log only if ini asks for it
        Log_Init("PadWithin.log");
    LOG("EnsureInit: deferred init (log enabled via ini)");
    // NOTE: Proxy_HookGameIAT_GetProcAddress() was a one-off diagnostic used
    // during axis-detection investigation (checking for dynamic legacy-
    // joystick/XInput API resolution). It's no longer needed — the real fix
    // is AxisSnapRatio — and its manual PE import-table parsing is fragile
    // across differently-packed executables (confirmed to crash on a GOG
    // build unpacked via a different UPX pass than the Steam build). Left
    // unused in iat_hook.cpp for reference; do not call it in production.
    LoadRealDInput();                  // load system dinput8
    ChainAsiLoader();                  // optional widescreen ASI chain
    Proxy_InitInput();                 // bring up SDL2
}

extern "C" HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
    EnsureInit();

    if (!g_realDI8Create) {
        LOG("DirectInput8Create: real dinput8 unavailable, returning E_FAIL");
        return E_FAIL;
    }

    HRESULT hr = g_realDI8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    LOG("DirectInput8Create -> hr=0x%08lX, ppvOut=%p", hr, ppvOut ? *ppvOut : nullptr);

    if (SUCCEEDED(hr) && ppvOut && *ppvOut) {
        // Wrap / hook the IDirectInput8 vtable so we can intercept CreateDevice.
        Proxy_HookDirectInput8(ppvOut);
    }
    return hr;
}

// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_selfModule = hModule;
        // Nothing else here: log opening and all init are deferred to the first
        // DirectInput8Create (EnsureInit) — both to honor the ini log toggle and
        // to stay out of the loader lock.
        break;
    case DLL_PROCESS_DETACH:
        Proxy_ShutdownInput();
        if (g_asiChain)   FreeLibrary(g_asiChain);
        if (g_realDInput) FreeLibrary(g_realDInput);
        LOG("=== detach ===");
        Log_Close();
        break;
    }
    return TRUE;
}
