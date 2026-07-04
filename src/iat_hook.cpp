// =============================================================================
//  iat_hook.cpp  -  patch the GAME EXE's own IAT entry for GetProcAddress so we
//  can see every function name it resolves dynamically at runtime.
//
//  Why: we've proven our DirectInput hooks receive correct data and are never
//  bypassed via GetDeviceData/Poll/SetEventNotification, yet the axis-binding
//  screen still misbehaves. The remaining explanation is that WW reads the
//  joystick through a completely different API during that screen — legacy
//  Windows Joystick API (winmm.dll joyGetPosEx/joyGetPos), XInput, or raw HID
//  — resolved dynamically via GetProcAddress rather than a static import (we
//  confirmed winmm.dll's static imports contain no joystick functions).
//
//  This hook patches pop2.exe's own IAT slot for KERNEL32.DLL!GetProcAddress,
//  so every subsequent GetProcAddress call the GAME makes (not just ours)
//  passes through our thunk first, which logs interesting names before
//  forwarding to the real function unchanged.
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include "log.h"
#include "config.h"

using GetProcAddress_t = FARPROC (WINAPI*)(HMODULE, LPCSTR);
static GetProcAddress_t g_realGetProcAddress = nullptr;

// Names worth flagging: anything joystick/gamepad/XInput/RawInput related.
static bool Interesting(const char* name)
{
    if (!name) return false;
    // ordinal imports pass a value < 0x10000 cast to LPCSTR; guard against that.
    if (reinterpret_cast<uintptr_t>(name) < 0x10000) return false;
    struct { const char* s; } needles[] = {
        {"joy"}, {"Joy"}, {"XInput"}, {"RawInput"}, {"GetRawInput"},
        {"DirectInput"}, {"HidD_"}, {"HidP_"}
    };
    for (auto& n : needles)
        if (strstr(name, n.s)) return true;
    return false;
}

static FARPROC WINAPI Hook_GetProcAddress(HMODULE hMod, LPCSTR name)
{
    FARPROC result = g_realGetProcAddress(hMod, name);
    if (Interesting(name)) {
        char modName[MAX_PATH] = {};
        GetModuleFileNameA(hMod, modName, sizeof(modName));
        LOG("GetProcAddress('%s') from module '%s' -> %p", name, modName, (void*)result);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Walk the main EXE's own import directory, find the IAT thunk that holds
// KERNEL32.DLL!GetProcAddress, and overwrite it with our hook.
// ---------------------------------------------------------------------------
void Proxy_HookGameIAT_GetProcAddress()
{
    HMODULE exeMod = GetModuleHandleA(nullptr);
    if (!exeMod) { LOG("IAT hook: GetModuleHandle(NULL) failed"); return; }

    auto base = reinterpret_cast<BYTE*>(exeMod);
    auto dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { LOG("IAT hook: bad DOS header"); return; }
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { LOG("IAT hook: bad NT header"); return; }

    auto& importDirEntry = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDirEntry.VirtualAddress == 0) { LOG("IAT hook: no import directory"); return; }

    auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDirEntry.VirtualAddress);
    for (; imp->Name; ++imp) {
        const char* dllName = reinterpret_cast<const char*>(base + imp->Name);
        if (_stricmp(dllName, "KERNEL32.dll") != 0) continue;

        auto thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (imp->FirstThunk ? imp->FirstThunk : imp->OriginalFirstThunk));
        auto origThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));

        for (; origThunk->u1.AddressOfData; ++origThunk, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) continue;
            auto byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + origThunk->u1.AddressOfData);
            if (strcmp(byName->Name, "GetProcAddress") != 0) continue;

            g_realGetProcAddress = reinterpret_cast<GetProcAddress_t>(thunk->u1.Function);
            DWORD oldProt;
            if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProt)) {
                thunk->u1.Function = reinterpret_cast<uintptr_t>(&Hook_GetProcAddress);
                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProt, &oldProt);
                LOG("IAT hook: patched exe's GetProcAddress IAT slot, orig=%p",
                    (void*)g_realGetProcAddress);
            } else {
                LOG("IAT hook: VirtualProtect failed on GetProcAddress slot");
            }
            return;
        }
    }
    LOG("IAT hook: KERNEL32.dll!GetProcAddress import not found");
}
