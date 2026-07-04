// =============================================================================
//  input_sdl.cpp  -  SDL2 GameController -> DIJOYSTATE2 translation
//
//  This is where the actual fixes live. The two WW-specific problems:
//
//   1. TRIGGERS share one Z-axis. On a real DirectInput pad, L2 and R2 push a
//      single Z axis in opposite directions, so pressing both cancels out.
//      Per the PS2 manual, WW uses L2 (Alternate view) and R2 (First-person
//      look) as pure ON/OFF actions, not analog. So instead of fighting the
//      shared axis, we read SDL's two INDEPENDENT trigger axes and feed them as
//      digital buttons past a threshold -> two independent bits in rgbButtons,
//      nothing to cancel out.
//
//   2. DIAGONALS die because the game (or the default Gamepads.DAT profile)
//      applies a coarse deadzone that clips the corners of the stick. We feed
//      a clean radial deadzone and pass through full-range X/Y so diagonals
//      survive.
//
//  Full control scheme (from the PS2 manual) drives the button map below.
//
//  Everything below is a compilable STUB: it builds and runs, opens the first
//  controller, and fills state, but the axis assignments and ranges are
//  placeholders (marked TODO) pending the Ghidra confirmation step.
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <SDL.h>

#include "proxy.h"
#include "log.h"
#include "config.h"

static SDL_GameController* g_pad = nullptr;
static SDL_Joystick*       g_joy = nullptr;   // raw fallback / underlying handle
#define MAX_PADS 8
static SDL_GameController* g_pads[MAX_PADS] = {};
static int                 g_padCount = 0;

// Deadzone and axis handling now come from config (g_cfg) / AxisDI().

// ---------------------------------------------------------------------------
void Proxy_InitInput()
{
    // Hints must be set BEFORE SDL_Init. In XInput mode the pad is best reached
    // via SDL's XInput backend; make sure joystick events pump even without an
    // SDL-owned window (we are a DLL inside someone else's process).
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");          // background poll thread
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");  // key: read while game has focus

    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        LOG("SDL_Init failed: %s", SDL_GetError());
        return;
    }

    int nj = SDL_NumJoysticks();
    LOG("SDL sees %d joystick(s):", nj);
    for (int i = 0; i < nj; ++i) {
        LOG("  joy[%d] name='%s' isGC=%d",
            i, SDL_JoystickNameForIndex(i), SDL_IsGameController(i));
    }

    // Open ALL game controllers so we can pick whichever is actually in use
    // (the user may have several pam present, some idle/ghost devices).
    for (int i = 0; i < nj && g_padCount < MAX_PADS; ++i) {
        if (SDL_IsGameController(i)) {
            SDL_GameController* c = SDL_GameControllerOpen(i);
            if (c) {
                g_pads[g_padCount++] = c;
                LOG("opened GameController %d: %s", i, SDL_GameControllerName(c));
            } else {
                LOG("SDL_GameControllerOpen(%d) failed: %s", i, SDL_GetError());
            }
        }
    }

    if (g_cfg.controllerIndex >= 0 && g_cfg.controllerIndex < g_padCount) {
        g_pad = g_pads[g_cfg.controllerIndex];
        LOG("using controller by ini index %d: %s",
            g_cfg.controllerIndex, SDL_GameControllerName(g_pad));
    } else if (g_padCount > 0) {
        g_pad = g_pads[0];   // default; may switch to the active one at runtime
        LOG("auto mode: defaulting to controller 0, will switch to active pad");
    }

    // Fallback: open as raw joystick if GameController mapping didn't take.
    if (g_padCount == 0 && nj > 0) {
        g_joy = SDL_JoystickOpen(0);
        if (g_joy)
            LOG("opened raw Joystick 0: %s (axes=%d buttons=%d hats=%d)",
                SDL_JoystickName(g_joy), SDL_JoystickNumAxes(g_joy),
                SDL_JoystickNumButtons(g_joy), SDL_JoystickNumHats(g_joy));
    }
    if (!g_pad && !g_joy) LOG("no SDL device opened at init");
}

// Pick the controller actually producing input (for auto mode with several
// pads, e.g. a ghost DualSense that's powered off but still enumerated).
static void SelectActivePad()
{
    if (g_cfg.controllerIndex >= 0) return;   // explicit choice, don't override
    if (g_padCount <= 1) return;
    for (int i = 0; i < g_padCount; ++i) {
        SDL_GameController* c = g_pads[i];
        if (!c) continue;
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; ++a) {
            if (abs(SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis)a)) > 12000) {
                if (g_pad != c) LOG("switching active pad to %d: %s",
                                    i, SDL_GameControllerName(c));
                g_pad = c; return;
            }
        }
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
            if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)b)) {
                if (g_pad != c) LOG("switching active pad to %d: %s",
                                    i, SDL_GameControllerName(c));
                g_pad = c; return;
            }
        }
    }
}

void Proxy_ShutdownInput()
{
    for (int i = 0; i < g_padCount; ++i)
        if (g_pads[i]) SDL_GameControllerClose(g_pads[i]);
    g_padCount = 0; g_pad = nullptr;
    if (g_joy) { SDL_JoystickClose(g_joy); g_joy = nullptr; }
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
}

// ---------------------------------------------------------------------------
// DIAGNOSTIC: pump events, then log state via BOTH the GameController API and
// the raw Joystick API, so we can tell whether SDL sees the pad at all and
// which layer has live data.
// ---------------------------------------------------------------------------
void Proxy_LogRawPad()
{
    SDL_PumpEvents();
    SDL_GameControllerUpdate();
    SDL_JoystickUpdate();

    char buf[768]; int n = 0;
    n += snprintf(buf+n, sizeof(buf)-n, "  [gc] ");
    if (g_pad) {
        n += snprintf(buf+n, sizeof(buf)-n, "btn:");
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
            if (SDL_GameControllerGetButton(g_pad, (SDL_GameControllerButton)b))
                n += snprintf(buf+n, sizeof(buf)-n, " %s",
                    SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b));
        n += snprintf(buf+n, sizeof(buf)-n, " axis:");
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; ++a) {
            int v = SDL_GameControllerGetAxis(g_pad, (SDL_GameControllerAxis)a);
            if (v > 8000 || v < -8000)
                n += snprintf(buf+n, sizeof(buf)-n, " %s=%d",
                    SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)a), v);
        }
    } else {
        n += snprintf(buf+n, sizeof(buf)-n, "(no GameController)");
    }

    // raw joystick view
    SDL_Joystick* j = g_joy ? g_joy : (g_pad ? SDL_GameControllerGetJoystick(g_pad) : nullptr);
    n += snprintf(buf+n, sizeof(buf)-n, " | [joy] ");
    if (j) {
        n += snprintf(buf+n, sizeof(buf)-n, "btn:");
        int nb = SDL_JoystickNumButtons(j);
        for (int b = 0; b < nb; ++b)
            if (SDL_JoystickGetButton(j, b))
                n += snprintf(buf+n, sizeof(buf)-n, " %d", b);
        n += snprintf(buf+n, sizeof(buf)-n, " axis:");
        int na = SDL_JoystickNumAxes(j);
        for (int a = 0; a < na; ++a) {
            int v = SDL_JoystickGetAxis(j, a);
            if (v > 8000 || v < -8000)
                n += snprintf(buf+n, sizeof(buf)-n, " a%d=%d", a, v);
        }
    } else {
        n += snprintf(buf+n, sizeof(buf)-n, "(no joystick handle)");
    }
    LOG("%s", buf);
}

// Apply a radial deadzone to a stick pair, preserving diagonals.
static void RadialDeadzone(float& x, float& y, float dz)
{
    float mag = std::sqrt(x*x + y*y);
    if (mag < dz) { x = y = 0.f; return; }
    // rescale so the edge of the deadzone maps to 0 and full stays full
    float scaled = (mag - dz) / (1.f - dz);
    float k = scaled / mag;
    x *= k; y *= k;
}

// Outer calibration: if a worn/loose stick's real full-press never quite
// reaches raw 1.0, rescale so `maxInputPercent` (e.g. 90) counts as full
// deflection. 100 is a no-op. Applied BEFORE deadzone, per axis.
static void ApplyMaxInput(float& x, float& y, int maxInputPercent)
{
    if (maxInputPercent >= 100) return;   // off
    float maxInput = maxInputPercent / 100.f;
    if (maxInput < 0.5f) maxInput = 0.5f;   // sanity guard
    x = x / maxInput; if (x > 1.f) x = 1.f; if (x < -1.f) x = -1.f;
    y = y / maxInput; if (y > 1.f) y = 1.f; if (y < -1.f) y = -1.f;
}

// Snap the minor axis to exactly zero when the stick is pushed mostly along
// one axis. Real hardware always leaks a tiny amount onto the "other" axis
// during a nominally pure push (mechanical tolerance), which WW's axis-BINDING
// detector treats as "that axis moved" if it scans axes in a fixed order and
// stops at the first one past a tiny threshold — starving the real axis even
// when its deflection is much larger. This does NOT affect true diagonal input
// (both axes comparable magnitude survive untouched).
static void AxisSnap(float& x, float& y)
{
    float ratio = g_cfg.axisSnapRatio;
    if (ratio <= 0.f) return;   // disabled
    float ax = std::fabs(x), ay = std::fabs(y);
    if (ax < ay * ratio) x = 0.f;
    else if (ay < ax * ratio) y = 0.f;
}

// ---------------------------------------------------------------------------
// Fill a DIJOYSTATE (80 bytes) from SDL GameController state.
//
// WW polls with cbData=80 == sizeof(DIJOYSTATE) (confirmed from logs), so this
// is the struct we synthesize. Mapping was captured empirically (see project
// notes); axis directions verified non-inverted (stick right/down -> +32767).
//
//   MOVEMENT  left stick  -> lX / lY          (WW: X / Y Axis)
//   CAMERA    right stick -> lRx / lRy        (WW: RX / RY Axis)
//   TRIGGERS  RT -> lZ(+) , LT -> lZ(-)       (WW: Camera Look / Alternate View
//                                              share the Z axis; opposite signs
//                                              is exactly what the game wants)
//   BUTTONS   SDL button  -> rgbButtons[WW_Btn - 1]   (PS layout, Cross=jump)
//   D-PAD     -> POV hat rgdwPOV[0]
//
// We use SDL's full DirectInput axis range (-32768..32767): that's what the
// real DI device reports, so WW's internal DIPROP_RANGE handling stays happy.
// ---------------------------------------------------------------------------

// Map normalized -1..1 to DirectInput signed axis range.
static long AxisDI(float norm)
{
    if (norm < -1.f) norm = -1.f;
    if (norm >  1.f) norm =  1.f;
    long v = (long)lroundf(norm * 32767.f);
    if (v >  32767) v =  32767;
    if (v < -32768) v = -32768;
    return v;
}

void Proxy_FillJoyState(DIJOYSTATE* js)
{
    if (!js) return;
    SDL_GameControllerUpdate();
    SelectActivePad();        // auto-pick the pad actually in use
    if (!g_pad) return;

    // zero everything; POV centering happens at the enumerated POV offset below
    memset(js, 0, sizeof(DIJOYSTATE));

    // CRITICAL: after SetDataFormat(c_dfDIJoystick2), DirectInput remaps the
    // device's native layout into the game's STANDARD format. The native
    // passthrough dump confirmed: horizontal lands at offset 0 (lX) and
    // vertical at offset 4 (lY) in the game-facing buffer, regardless of the
    // device's enum dwOfs. So we write plain standard fields.
    unsigned char* base = reinterpret_cast<unsigned char*>(js);
    auto W = [&](int axisIdx, int stdOfs, long val){
        (void)axisIdx;   // enum offsets are device-native, not buffer offsets
        if (stdOfs >= 0 && stdOfs + (int)sizeof(long) <= (int)sizeof(DIJOYSTATE))
            *reinterpret_cast<long*>(base + stdOfs) = val;
    };

    // --- movement: left stick -> X / Y ---
    float lx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.f;
    float ly = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.f;
    ApplyMaxInput(lx, ly, g_cfg.moveMaxRange);
    RadialDeadzone(lx, ly, g_cfg.moveDeadzone);
    AxisSnap(lx, ly);
    if (g_cfg.invertMoveY) ly = -ly;
    W(0, 0, AxisDI(lx));   // X
    W(1, 4, AxisDI(ly));   // Y

    // --- camera: right stick -> Rx / Ry (per WW's default mapping) ---
    // CameraSensitivity is a percent where 50 = normal speed (1.0x).
    float camMul = g_cfg.cameraSensitivity / 50.f;
    if (camMul < 0.f) camMul = 0.f;
    float rx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.f;
    float ry = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.f;
    ApplyMaxInput(rx, ry, g_cfg.cameraMaxRange);
    RadialDeadzone(rx, ry, g_cfg.cameraDeadzone);
    AxisSnap(rx, ry);
    rx *= camMul;
    ry *= camMul;
    if (g_cfg.invertCameraX) rx = -rx;
    if (g_cfg.invertCameraY) ry = -ry;

    // --- triggers -> Z (Camera Look / Alternate View) ---
    float rt = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.f;
    float lt = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  / 32767.f;
    if (rt < 0.f) rt = 0.f;
    if (lt < 0.f) lt = 0.f;
    float z = g_cfg.swapTriggers ? (lt - rt) : (rt - lt);

    if (!g_cfg.cameraOnZRz) {
        W(3, 12, AxisDI(rx));  // Rx camera H
        W(4, 16, AxisDI(ry));  // Ry camera V
        W(2, 8,  AxisDI(z));   // Z  triggers
    } else {
        W(2, 8,  AxisDI(rx));
        W(5, 20, AxisDI(ry));
        W(3, 12, AxisDI(z));
    }

    // --- buttons: SDL GameController -> button at enumerated offset ---
    auto set = [&](SDL_GameControllerButton b, int idx){
        if (idx < 0 || idx >= 32) return;
        if (!SDL_GameControllerGetButton(g_pad, b)) return;
        int ofs = 48 + idx;   // standard rgbButtons (buffer is game-format)
        if (ofs >= 0 && ofs < (int)sizeof(DIJOYSTATE)) base[ofs] = 0x80;
    };
    set(SDL_CONTROLLER_BUTTON_A,             g_cfg.btnA);
    set(SDL_CONTROLLER_BUTTON_B,             g_cfg.btnB);
    set(SDL_CONTROLLER_BUTTON_X,             g_cfg.btnX);
    set(SDL_CONTROLLER_BUTTON_Y,             g_cfg.btnY);
    set(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  g_cfg.btnLB);
    set(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, g_cfg.btnRB);
    set(SDL_CONTROLLER_BUTTON_START,         g_cfg.btnStart);
    set(SDL_CONTROLLER_BUTTON_BACK,          g_cfg.btnBack);
    set(SDL_CONTROLLER_BUTTON_LEFTSTICK,     g_cfg.btnLS);
    set(SDL_CONTROLLER_BUTTON_RIGHTSTICK,    g_cfg.btnRS);

    // --- D-pad -> POV hat (hundredths of a degree, clockwise from up) ---
    bool up    = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
    bool down  = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    bool left  = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    bool right = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    DWORD pov = 0xFFFFFFFF;
    if      (up && right) pov = 4500;
    else if (right&&down) pov = 13500;
    else if (down&&left)  pov = 22500;
    else if (left&&up)    pov = 31500;
    else if (up)          pov = 0;
    else if (right)       pov = 9000;
    else if (down)        pov = 18000;
    else if (left)        pov = 27000;
    {
        int povOfs = 32;      // standard rgdwPOV[0] (buffer is game-format)
        if (povOfs >= 0 && povOfs + (int)sizeof(DWORD) <= (int)sizeof(DIJOYSTATE))
            *reinterpret_cast<DWORD*>(base + povOfs) = pov;
    }

    // Optional axis diagnostics (only when EnableLog=1). Throttled.
    if (g_cfg.enableLog) {
        static DWORD last = 0; DWORD now = GetTickCount();
        if (now - last > 250) {
            last = now;
            // Named fields AND raw longs read straight from the byte offsets the
            // game's data format uses (X@0 Y@4 Z@8 Rx@12 Ry@16 Rz@20), so we can
            // confirm lY really lands at offset 4.
            int slx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX);
            int sly = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY);
            const unsigned char* b = reinterpret_cast<const unsigned char*>(js);
            long o0  = *reinterpret_cast<const long*>(b + 0);
            long o4  = *reinterpret_cast<const long*>(b + 4);
            long o8  = *reinterpret_cast<const long*>(b + 8);
            long o12 = *reinterpret_cast<const long*>(b + 12);
            LOG("  [fill] SDL_LX=%d SDL_LY=%d -> buf ofs0=%ld ofs4=%ld ofs8=%ld ofs12=%ld",
                slx, sly, o0, o4, o8, o12);
        }
    }
}
